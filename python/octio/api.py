"""High-level Python API for FDA and E2E OCT files.

The wrapper uses the C ABI exposed by the C++ library.  NumPy and Pillow are
optional runtime dependencies: the native reader itself has no third-party
runtime dependency.
"""

from __future__ import annotations

import ctypes as ct
import io
import json
from pathlib import Path
from typing import Any, Iterator

from . import _native as n

_DTYPE_TO_CT = {1: ct.c_uint8, 2: ct.c_uint16, 3: ct.c_float}
_DTYPE_TO_NP = {1: "uint8", 2: "uint16", 3: "float32"}


def _b(value: str | None) -> bytes | None:
  """Encode an optional Python string as UTF-8.

  Parameters
  ----------
  value : str or None
    Optional text.

  Returns
  -------
  bytes or None
    UTF-8 representation suitable for the C ABI.
  """

  return None if value is None else value.encode("utf-8")


def _numpy() -> Any:
  """Import NumPy lazily.

  Returns
  -------
  module
    Imported :mod:`numpy` module.

  Raises
  ------
  ImportError
    If NumPy is not installed.
  """

  import numpy as np
  return np


def _array_from_info(info: n.Array2DInfo, copy: bool = False) -> Any:
  """Create a NumPy array from a native 2-D view.

  Parameters
  ----------
  info : Array2DInfo
    Native array descriptor.
  copy : bool, default=False
    Copy pixels instead of returning a zero-copy mapped view.

  Returns
  -------
  numpy.ndarray
    Two-dimensional array with native row stride.
  """

  np = _numpy()
  if not info.data:
    return np.empty((0, 0), dtype=_DTYPE_TO_NP[info.dtype])
  itemsize = np.dtype(_DTYPE_TO_NP[info.dtype]).itemsize
  byte_count = max(1, (info.height - 1) * info.row_stride_bytes + info.width * itemsize)
  buf = (ct.c_uint8 * byte_count).from_address(info.data)
  arr = np.ndarray(
    shape=(info.height, info.width),
    dtype=_DTYPE_TO_NP[info.dtype],
    buffer=buf,
    strides=(info.row_stride_bytes, itemsize),
  )
  return arr.copy() if copy else arr


class Volume:
  """Logical OCT volume backed by a native document.

  Parameters
  ----------
  owner : OCTFile
    Parent document that owns the mapped memory.
  index : int
    Native volume index.
  """

  def __init__(self, owner: "OCTFile", index: int) -> None:
    """Initialize a volume proxy.

    Parameters
    ----------
    owner : OCTFile
      Parent file.
    index : int
      Volume index.
    """

    self._owner = owner
    self.index = index

  @property
  def id(self) -> str:
    """str: Stable native volume identifier."""
    raw = n.lib.octio_volume_id(self._owner._handle, self.index)
    if not raw:
      n.check(-1)
    return raw.decode("utf-8", "replace")

  @property
  def num_slices(self) -> int:
    """int: Number of available B-scans."""
    return int(n.lib.octio_volume_slice_count(self._owner._handle, self.index))

  def info(self) -> dict[str, Any]:
    """Return volume metadata and native slice geometry.

    Returns
    -------
    dict
      Volume identifier, number of B-scans, representative shape,
      storage mode, and whether E2E custom-float decoding is required.
    """
    if self.num_slices == 0:
      shape = (0, 0)
      storage = "empty"
    else:
      info = n.Array2DInfo()
      status = n.lib.octio_volume_slice(
        self._owner._handle, self.index, 0, ct.byref(info)
      )
      if status == 0:
        shape = (int(info.height), int(info.width))
        storage = "raw"
      else:
        ptr, size = ct.c_void_p(), ct.c_size_t()
        height, width, encoding = ct.c_size_t(), ct.c_size_t(), ct.c_int()
        n.check(n.lib.octio_volume_slice_encoded(
          self._owner._handle, self.index, 0, ct.byref(ptr), ct.byref(size),
          ct.byref(height), ct.byref(width), ct.byref(encoding),
        ))
        shape = (int(height.value), int(width.value))
        storage = "jpeg" if encoding.value == 1 else "encoded"
    return {
      "id": self.id,
      "num_slices": self.num_slices,
      "shape_per_slice": shape,
      "storage": storage,
      "e2e_custom_float": self.is_e2e_custom_float,
    }

  @property
  def is_e2e_custom_float(self) -> bool:
    """bool: Whether uint16 pixels encode Heidelberg custom floats."""
    return bool(n.lib.octio_volume_is_e2e_custom_float(self._owner._handle, self.index))

  def slice(self, index: int, copy: bool = False) -> Any:
    """Return one B-scan as a zero-copy NumPy array when possible.

    Parameters
    ----------
    index : int
      B-scan index.
    copy : bool, default=False
      Return an independent copy instead of a mapped view.

    Returns
    -------
    numpy.ndarray
      Native pixels. E2E pixels remain encoded ufloat16 values; use
      :meth:`slice_float32` for decoded display intensities.
    """

    info = n.Array2DInfo()
    status = n.lib.octio_volume_slice(self._owner._handle, self.index, index, ct.byref(info))
    if status == 0:
      return _array_from_info(info, copy=copy)

    # JPEG-compressed FDA volumes are deliberately decoded in Python so
    # the C++ core has no mandatory codec dependency.
    ptr, size = ct.c_void_p(), ct.c_size_t()
    height, width, encoding = ct.c_size_t(), ct.c_size_t(), ct.c_int()
    n.check(n.lib.octio_volume_slice_encoded(
      self._owner._handle, self.index, index, ct.byref(ptr), ct.byref(size),
      ct.byref(height), ct.byref(width), ct.byref(encoding),
    ))
    if encoding.value != 1:
      raise RuntimeError("Unsupported encoded B-scan representation")
    try:
      from PIL import Image
    except ImportError as exc:
      raise ImportError("Pillow is required to decode IMG_JPEG B-scans") from exc
    np = _numpy()
    arr = np.asarray(Image.open(io.BytesIO(ct.string_at(ptr.value, size.value))))
    return arr.copy() if copy else arr

  def slice_float32(self, index: int, decode_e2e: bool = True) -> Any:
    """Copy and decode one B-scan to float32.

    Parameters
    ----------
    index : int
      B-scan index.
    decode_e2e : bool, default=True
      Decode Heidelberg ufloat16 and apply its common display transform.

    Returns
    -------
    numpy.ndarray
      Float32 array with shape ``(depth, A-scans)``.
    """

    np = _numpy()
    raw = self.slice(index, copy=False)
    out = np.empty(raw.shape, dtype=np.float32)
    status = n.lib.octio_copy_bscan_f32(
      self._owner._handle, self.index, index,
      out.ctypes.data_as(ct.POINTER(ct.c_float)), out.size,
      int(decode_e2e),
    )
    if status == 0:
      return out
    # Encoded FDA JPEG slices are already decoded by ``slice``.
    return raw.astype(np.float32, copy=True)

  def a_scan(self, slice_index: int, x_index: int, decode_e2e: bool = True) -> Any:
    """Return one depth profile as float32.

    Parameters
    ----------
    slice_index : int
      B-scan index.
    x_index : int
      A-scan/column index.
    decode_e2e : bool, default=True
      Decode Heidelberg ufloat16 values when reading E2E.

    Returns
    -------
    numpy.ndarray
      One-dimensional depth profile.
    """

    np = _numpy()
    image = self.slice(slice_index, copy=False)
    shape = image.shape
    if x_index < 0 or x_index >= shape[1]:
      raise IndexError("x_index is outside the B-scan width")
    out = np.empty(shape[0], dtype=np.float32)
    status = n.lib.octio_copy_ascan_f32(
      self._owner._handle, self.index, slice_index, x_index,
      out.ctypes.data_as(ct.POINTER(ct.c_float)), out.size,
      int(decode_e2e),
    )
    if status == 0:
      return out
    return image[:, x_index].astype(np.float32, copy=True)

  def as_numpy(self, decode_e2e: bool = True) -> Any:
    """Materialize the complete volume as ``(slice, depth, A-scan)``.

    Parameters
    ----------
    decode_e2e : bool, default=True
      Decode Heidelberg custom intensities into float32. FDA volumes are
      returned as uint16 when ``False`` and float32 when ``True``.

    Returns
    -------
    numpy.ndarray
      Dense three-dimensional volume.
    """

    np = _numpy()
    if self.num_slices == 0:
      return np.empty((0, 0, 0), dtype=np.float32)
    if self.is_e2e_custom_float and decode_e2e:
      return np.stack([self.slice_float32(i, True) for i in range(self.num_slices)])
    return np.stack([self.slice(i, copy=True) for i in range(self.num_slices)])


class FundusImage:
  """Fundus or localizer image proxy."""

  def __init__(self, owner: "OCTFile", index: int) -> None:
    """Initialize a fundus proxy.

    Parameters
    ----------
    owner : OCTFile
      Parent file.
    index : int
      Image index.
    """
    self._owner = owner
    self.index = index

  def info(self) -> dict[str, Any]:
    """Return image metadata.

    Returns
    -------
    dict
      Width, height, channel count, scalar type and encoding.
    """
    x = n.FundusInfo()
    n.check(n.lib.octio_fundus(self._owner._handle, self.index, ct.byref(x)))
    return {
      "width": int(x.width), "height": int(x.height), "channels": int(x.channels),
      "byte_size": int(x.byte_size), "dtype": _DTYPE_TO_NP.get(x.dtype, "unknown"),
      "encoding": "jpeg" if x.encoding == 1 else "raw",
    }

  def bytes(self) -> bytes:
    """Return encoded/native image bytes.

    Returns
    -------
    bytes
      JPEG bytes for FDA encoded images or raw pixels for E2E images.
    """
    x = n.FundusInfo()
    n.check(n.lib.octio_fundus(self._owner._handle, self.index, ct.byref(x)))
    return ct.string_at(x.data, x.byte_size)

  def decode(self) -> Any:
    """Decode the image to a NumPy array.

    Returns
    -------
    numpy.ndarray
      Decoded image.

    Notes
    -----
    FDA JPEG decoding uses Pillow only in Python and therefore keeps the C++
    core free of a mandatory JPEG dependency.
    """
    np = _numpy()
    x = n.FundusInfo()
    n.check(n.lib.octio_fundus(self._owner._handle, self.index, ct.byref(x)))
    if x.encoding == 1:
      try:
        from PIL import Image
      except ImportError as exc:
        raise ImportError("Pillow is required to decode FDA JPEG fundus images") from exc
      return np.asarray(Image.open(io.BytesIO(ct.string_at(x.data, x.byte_size))))
    itemsize = np.dtype(_DTYPE_TO_NP[x.dtype]).itemsize
    count = x.height * x.width * x.channels
    buf_type = _DTYPE_TO_CT[x.dtype] * count
    arr = np.ctypeslib.as_array(buf_type.from_address(x.data))
    shape = (x.height, x.width) if x.channels == 1 else (x.height, x.width, x.channels)
    return arr.reshape(shape)


class Segmentation:
  """Retinal layer segmentation or boundary proxy."""

  def __init__(self, owner: "OCTFile", index: int) -> None:
    """Initialize a segmentation proxy.

    Parameters
    ----------
    owner : OCTFile
      Parent file.
    index : int
      Segmentation index.
    """
    self._owner = owner
    self.index = index

  @property
  def layer(self) -> str:
    """str: Canonical layer/boundary name when known."""
    raw = n.lib.octio_segmentation_name(self._owner._handle, self.index)
    if not raw:
      n.check(-1)
    return raw.decode("utf-8", "replace")

  def info(self) -> dict[str, Any]:
    """Return segmentation metadata.

    Returns
    -------
    dict
      Layer ID, slice index, convention and array shape.
    """
    x = n.SegmentationInfo()
    n.check(n.lib.octio_segmentation(self._owner._handle, self.index, ct.byref(x)))
    return {
      "layer": self.layer, "layer_id": int(x.layer_id), "slice_index": int(x.slice_index),
      "height": int(x.height), "width": int(x.width),
      "measured_from_bottom": bool(x.measured_from_bottom),
      "dtype": _DTYPE_TO_NP.get(x.dtype, "unknown"),
    }

  def as_numpy(self, copy: bool = False) -> Any:
    """Return segmentation values as a NumPy array.

    Parameters
    ----------
    copy : bool, default=False
      Copy data rather than referencing mapped file bytes.

    Returns
    -------
    numpy.ndarray
      Layer map/boundary values in native units.
    """
    x = n.SegmentationInfo()
    n.check(n.lib.octio_segmentation(self._owner._handle, self.index, ct.byref(x)))
    a = n.Array2DInfo(x.data, x.height, x.width, x.row_stride_bytes, x.dtype)
    return _array_from_info(a, copy=copy)


class OCTFile:
  """Unified high-level FDA/E2E document.

  Parameters
  ----------
  path : str or pathlib.Path
    FDA or E2E file to open.
  """

  def __init__(self, path: str | Path) -> None:
    """Open and index a native OCT file.

    Parameters
    ----------
    path : str or pathlib.Path
      FDA or E2E input file.
    """
    self.path = Path(path)
    self._handle = n.lib.octio_open(str(self.path).encode("utf-8"))
    if not self._handle:
      msg = n.lib.octio_last_error()
      raise RuntimeError(msg.decode("utf-8", "replace") if msg else "Cannot open OCT file")

  def close(self) -> None:
    """Close the native file and invalidate all zero-copy views."""
    if self._handle:
      n.lib.octio_close(self._handle)
      self._handle = None

  def __enter__(self) -> "OCTFile":
    """Enter a context manager and return this file.

    Returns
    -------
    OCTFile
      This open file.
    """
    return self

  def __exit__(self, exc_type: Any, exc: Any, tb: Any) -> None:
    """Close the file when leaving a context manager."""
    self.close()

  def __del__(self) -> None:
    """Best-effort native handle cleanup."""
    try:
      self.close()
    except Exception:
      pass

  @property
  def format(self) -> str:
    """str: ``'FDA'`` or ``'E2E'``."""
    return {1: "FDA", 2: "E2E"}.get(n.lib.octio_format(self._handle), "UNKNOWN")

  @property
  def metadata(self) -> dict[str, Any]:
    """dict: Decoded common and backend-specific metadata."""
    raw = n.lib.octio_metadata_json(self._handle)
    if not raw:
      n.check(-1)
    return json.loads(raw.decode("utf-8"))

  @property
  def chunks(self) -> list[dict[str, Any]]:
    """list of dict: Complete native chunk/data-element index."""
    raw = n.lib.octio_chunks_json(self._handle)
    if not raw:
      n.check(-1)
    return json.loads(raw.decode("utf-8"))

  @property
  def volumes(self) -> list[Volume]:
    """list of Volume: All OCT volumes in the file."""
    return [Volume(self, i) for i in range(n.lib.octio_volume_count(self._handle))]

  @property
  def fundus(self) -> list[FundusImage]:
    """list of FundusImage: Fundus/localizer images."""
    return [FundusImage(self, i) for i in range(n.lib.octio_fundus_count(self._handle))]

  @property
  def segmentations(self) -> list[Segmentation]:
    """list of Segmentation: Retinal layer boundaries/maps."""
    return [Segmentation(self, i) for i in range(n.lib.octio_segmentation_count(self._handle))]

  def chunk_bytes(self, index: int) -> bytes:
    """Return one complete native chunk body.

    Parameters
    ----------
    index : int
      Chunk index from :attr:`chunks`.

    Returns
    -------
    bytes
      Exact chunk/data-element body.
    """
    ptr, size = ct.c_void_p(), ct.c_size_t()
    n.check(n.lib.octio_chunk_bytes(self._handle, index, ct.byref(ptr), ct.byref(size)))
    return ct.string_at(ptr.value, size.value)

  def chunk_view(self, index: int) -> memoryview:
    """Return a zero-copy read-only-style view of a native chunk body.

    Parameters
    ----------
    index : int
      Chunk index from :attr:`chunks`.

    Returns
    -------
    memoryview
      View backed by the mapped source file or in-memory replacement.

    Notes
    -----
    Keep the :class:`OCTFile` open while using the returned view. Treat it
    as read-only; native source mappings are opened read-only.
    """
    ptr, size = ct.c_void_p(), ct.c_size_t()
    n.check(n.lib.octio_chunk_bytes(self._handle, index, ct.byref(ptr), ct.byref(size)))
    buf = (ct.c_uint8 * size.value).from_address(ptr.value)
    return memoryview(buf)

  def replace_chunk(self, index: int, data: bytes | bytearray | memoryview) -> None:
    """Replace a native chunk body.

    Parameters
    ----------
    index : int
      Chunk index.
    data : bytes-like
      Replacement bytes. E2E replacements must keep exactly the original
      byte count; FDA chunks are length-prefixed and may be resized.
    """
    raw = bytes(data)
    buf = (ct.c_uint8 * len(raw)).from_buffer_copy(raw) if raw else None
    n.check(n.lib.octio_replace_chunk(self._handle, index, buf, len(raw)))

  def set_patient(
    self,
    *,
    patient_id: str | None = None,
    first_name: str | None = None,
    middle_name: str | None = None,
    last_name: str | None = None,
    sex: str | None = None,
    birth_date: str | None = None,
  ) -> None:
    """Update patient metadata in the native writable representation.

    Parameters
    ----------
    patient_id : str, optional
      Patient identifier.
    first_name : str, optional
      Given name.
    middle_name : str, optional
      Middle name. E2E has no equivalent field and ignores it.
    last_name : str, optional
      Family name.
    sex : {'M', 'F', 'O'}, optional
      Sex code.
    birth_date : str, optional
      Date in ``YYYY-MM-DD`` format.
    """
    n.check(n.lib.octio_set_patient(
      self._handle, _b(patient_id), _b(first_name), _b(middle_name),
      _b(last_name), _b(sex), _b(birth_date),
    ))

  def set_fda_raw_volume(self, volume: Any, index: int = 0) -> None:
    """Replace the primary raw FDA OCT volume.

    Parameters
    ----------
    volume : array-like
      C-contiguous uint16 array with shape ``(slice, depth, A-scan)``.
    index : int, default=0
      FDA volume index.

    Notes
    -----
    Changing geometry may make existing registration and segmentation
    metadata inconsistent. The writer preserves unknown Topcon trailers.
    """
    np = _numpy()
    arr = np.ascontiguousarray(volume, dtype=np.uint16)
    if arr.ndim != 3:
      raise ValueError("volume must have shape (slice, depth, A-scan)")
    n.check(n.lib.octio_set_fda_raw_volume(
      self._handle, index,
      arr.ctypes.data_as(ct.POINTER(ct.c_uint16)),
      arr.shape[0], arr.shape[1], arr.shape[2],
    ))

  def set_fda_segmentation(self, layer: str, values: Any) -> None:
    """Replace or append an FDA ``CONTOUR_INFO`` layer map.

    Parameters
    ----------
    layer : str
      Canonical layer name such as ``'ILM'`` or native
      ``'MULTILAYERS_1'`` identifier.
    values : array-like
      C-contiguous uint16 array with shape ``(height, width)``.
    """
    np = _numpy()
    arr = np.ascontiguousarray(values, dtype=np.uint16)
    if arr.ndim != 2:
      raise ValueError("values must be two-dimensional")
    n.check(n.lib.octio_set_fda_segmentation(
      self._handle, layer.encode("utf-8"),
      arr.ctypes.data_as(ct.POINTER(ct.c_uint16)), arr.shape[0], arr.shape[1],
    ))

  def set_fda_fundus_jpeg(self, jpeg: bytes, width: int, height: int, channels: int = 3) -> None:
    """Replace or append an FDA fundus JPEG without a native codec dependency.

    Parameters
    ----------
    jpeg : bytes
      Already encoded JPEG stream.
    width : int
      Image width in pixels.
    height : int
      Image height in pixels.
    channels : int, default=3
      Nominal channel count used for the FDA header.
    """
    raw = bytes(jpeg)
    buf = (ct.c_uint8 * len(raw)).from_buffer_copy(raw)
    n.check(n.lib.octio_set_fda_fundus_jpeg(
      self._handle, buf, len(raw), height, width, channels,
    ))

  def save(self, path: str | Path) -> None:
    """Write a native-format copy with all current edits.

    Parameters
    ----------
    path : str or pathlib.Path
      Destination filename.

    Notes
    -----
    FDA output is rebuilt from the original chunk stream and preserves
    unknown chunks verbatim. E2E output is a byte-identical copy plus
    same-size patches because E2E directory offsets are absolute.
    """
    n.check(n.lib.octio_save(self._handle, str(path).encode("utf-8")))


def open_file(path: str | Path) -> OCTFile:
  """Open an FDA or E2E file using the unified interface.

  Parameters
  ----------
  path : str or pathlib.Path
    Input file.

  Returns
  -------
  OCTFile
    Open document.
  """
  return OCTFile(path)
