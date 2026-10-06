"""Low-level ctypes loader for :mod:`octio`.

The module intentionally avoids a compiled Python binding dependency.  The C++
shared library exposes a stable C ABI and NumPy is imported only by the high-
level wrapper when array materialization is requested.
"""

from __future__ import annotations

import ctypes as _ct
import ctypes.util as _ct_util
import os as _os
import sys as _sys
from pathlib import Path as _Path


class Array2DInfo(_ct.Structure):
  """C ABI descriptor for a two-dimensional array view."""

  _fields_ = [
    ("data", _ct.c_void_p),
    ("height", _ct.c_size_t),
    ("width", _ct.c_size_t),
    ("row_stride_bytes", _ct.c_ssize_t),
    ("dtype", _ct.c_int),
  ]


class FundusInfo(_ct.Structure):
  """C ABI descriptor for a fundus/localizer image."""

  _fields_ = [
    ("data", _ct.c_void_p),
    ("byte_size", _ct.c_size_t),
    ("height", _ct.c_size_t),
    ("width", _ct.c_size_t),
    ("channels", _ct.c_size_t),
    ("dtype", _ct.c_int),
    ("encoding", _ct.c_int),
  ]


class SegmentationInfo(_ct.Structure):
  """C ABI descriptor for a segmentation array."""

  _fields_ = [
    ("data", _ct.c_void_p),
    ("height", _ct.c_size_t),
    ("width", _ct.c_size_t),
    ("row_stride_bytes", _ct.c_ssize_t),
    ("dtype", _ct.c_int),
    ("layer_id", _ct.c_int),
    ("slice_index", _ct.c_size_t),
    ("measured_from_bottom", _ct.c_int),
  ]


def _candidate_names() -> list[str]:
  """Return platform-specific shared-library candidate names.

  Returns
  -------
  list of str
    Candidate filenames in lookup order.
  """

  if _sys.platform.startswith("win"):
    return ["octio.dll", "liboctio.dll"]
  if _sys.platform == "darwin":
    return ["liboctio.dylib", "octio.dylib"]
  return ["liboctio.so", "octio.so"]


def _package_search_dirs() -> list[_Path]:
  """Return package directories that may contain the native library.

  The normal wheel has a single package directory. A scikit-build-core
  redirect-mode editable install can expose two directories for the same
  package: the CMake install tree and the live Python source tree. Searching
  the package ``__path__`` and the editable loader paths makes both layouts
  work without ``PYTHONPATH`` or ``OCTIO_LIBRARY``.

  Returns
  -------
  list of pathlib.Path
    Existing or potential package directories in lookup order.
  """

  directories: list[_Path] = []

  def add(path: object) -> None:
    """Append one unique search directory."""
    if path is None:
      return
    candidate = _Path(path).resolve()
    if candidate not in directories:
      directories.append(candidate)

  add(_Path(__file__).resolve().parent)

  package = _sys.modules.get(__package__)
  if package is not None:
    for path in getattr(package, "__path__", ()):
      add(path)

    loader = getattr(package, "__loader__", None)
    for path in getattr(loader, "paths", ()):
      add(path)

  return directories


def _load_library() -> _ct.CDLL:
  """Load the native octio shared library.

  Returns
  -------
  ctypes.CDLL
    Loaded native library.

  Raises
  ------
  OSError
    If no compatible shared library can be found.
  """

  explicit = _os.environ.get("OCTIO_LIBRARY")
  candidates: list[_Path] = []
  if explicit:
    candidates.append(_Path(explicit))

  for directory in _package_search_dirs():
    for name in _candidate_names():
      candidates.append(directory / name)

  errors: list[str] = []
  for path in candidates:
    if not path.is_file():
      continue
    try:
      return _ct.CDLL(str(path))
    except OSError as exc:
      errors.append(f"{path}: {exc}")

  # Native CMake installs may be discoverable through the system loader.
  found = _ct_util.find_library("octio")
  if found:
    try:
      return _ct.CDLL(found)
    except OSError as exc:
      errors.append(f"{found}: {exc}")

  for name in _candidate_names():
    try:
      return _ct.CDLL(name)
    except OSError as exc:
      errors.append(f"{name}: {exc}")

  searched = ", ".join(str(path) for path in candidates) or "<none>"
  detail = f" Last loader error: {errors[-1]}" if errors else ""
  raise OSError(
    "octio native library was not found. Searched package locations: "
    f"{searched}.{detail} Install with `pip install .` or `pip install -e .`; "
    "for an uninstalled development build only, OCTIO_LIBRARY may point to "
    "the explicit shared-library path."
  )


lib = _load_library()

lib.octio_open.argtypes = [_ct.c_char_p]
lib.octio_open.restype = _ct.c_void_p
lib.octio_close.argtypes = [_ct.c_void_p]
lib.octio_last_error.restype = _ct.c_char_p
lib.octio_format.argtypes = [_ct.c_void_p]
lib.octio_format.restype = _ct.c_int
lib.octio_metadata_json.argtypes = [_ct.c_void_p]
lib.octio_metadata_json.restype = _ct.c_char_p
lib.octio_chunks_json.argtypes = [_ct.c_void_p]
lib.octio_chunks_json.restype = _ct.c_char_p
lib.octio_volume_count.argtypes = [_ct.c_void_p]
lib.octio_volume_count.restype = _ct.c_size_t
lib.octio_volume_id.argtypes = [_ct.c_void_p, _ct.c_size_t]
lib.octio_volume_id.restype = _ct.c_char_p
lib.octio_volume_is_e2e_custom_float.argtypes = [_ct.c_void_p, _ct.c_size_t]
lib.octio_volume_is_e2e_custom_float.restype = _ct.c_int
lib.octio_volume_slice_count.argtypes = [_ct.c_void_p, _ct.c_size_t]
lib.octio_volume_slice_count.restype = _ct.c_size_t
lib.octio_volume_slice.argtypes = [
  _ct.c_void_p,
  _ct.c_size_t,
  _ct.c_size_t,
  _ct.POINTER(Array2DInfo),
]
lib.octio_volume_slice.restype = _ct.c_int
lib.octio_volume_slice_encoded.argtypes = [
  _ct.c_void_p,
  _ct.c_size_t,
  _ct.c_size_t,
  _ct.POINTER(_ct.c_void_p),
  _ct.POINTER(_ct.c_size_t),
  _ct.POINTER(_ct.c_size_t),
  _ct.POINTER(_ct.c_size_t),
  _ct.POINTER(_ct.c_int),
]
lib.octio_volume_slice_encoded.restype = _ct.c_int
lib.octio_copy_bscan_f32.argtypes = [
  _ct.c_void_p,
  _ct.c_size_t,
  _ct.c_size_t,
  _ct.POINTER(_ct.c_float),
  _ct.c_size_t,
  _ct.c_int,
]
lib.octio_copy_bscan_f32.restype = _ct.c_int
lib.octio_copy_ascan_f32.argtypes = [
  _ct.c_void_p,
  _ct.c_size_t,
  _ct.c_size_t,
  _ct.c_size_t,
  _ct.POINTER(_ct.c_float),
  _ct.c_size_t,
  _ct.c_int,
]
lib.octio_copy_ascan_f32.restype = _ct.c_int
lib.octio_fundus_count.argtypes = [_ct.c_void_p]
lib.octio_fundus_count.restype = _ct.c_size_t
lib.octio_fundus.argtypes = [_ct.c_void_p, _ct.c_size_t, _ct.POINTER(FundusInfo)]
lib.octio_fundus.restype = _ct.c_int
lib.octio_segmentation_count.argtypes = [_ct.c_void_p]
lib.octio_segmentation_count.restype = _ct.c_size_t
lib.octio_segmentation.argtypes = [_ct.c_void_p, _ct.c_size_t, _ct.POINTER(SegmentationInfo)]
lib.octio_segmentation.restype = _ct.c_int
lib.octio_segmentation_name.argtypes = [_ct.c_void_p, _ct.c_size_t]
lib.octio_segmentation_name.restype = _ct.c_char_p
lib.octio_chunk_bytes.argtypes = [
  _ct.c_void_p,
  _ct.c_size_t,
  _ct.POINTER(_ct.c_void_p),
  _ct.POINTER(_ct.c_size_t),
]
lib.octio_chunk_bytes.restype = _ct.c_int
lib.octio_replace_chunk.argtypes = [_ct.c_void_p, _ct.c_size_t, _ct.c_void_p, _ct.c_size_t]
lib.octio_replace_chunk.restype = _ct.c_int
lib.octio_set_patient.argtypes = [_ct.c_void_p] + [_ct.c_char_p] * 6
lib.octio_set_patient.restype = _ct.c_int
lib.octio_set_fda_raw_volume.argtypes = [
  _ct.c_void_p,
  _ct.c_size_t,
  _ct.POINTER(_ct.c_uint16),
  _ct.c_size_t,
  _ct.c_size_t,
  _ct.c_size_t,
]
lib.octio_set_fda_raw_volume.restype = _ct.c_int
lib.octio_set_fda_segmentation.argtypes = [
  _ct.c_void_p,
  _ct.c_char_p,
  _ct.POINTER(_ct.c_uint16),
  _ct.c_size_t,
  _ct.c_size_t,
]
lib.octio_set_fda_segmentation.restype = _ct.c_int
lib.octio_set_fda_fundus_jpeg.argtypes = [
  _ct.c_void_p,
  _ct.POINTER(_ct.c_uint8),
  _ct.c_size_t,
  _ct.c_size_t,
  _ct.c_size_t,
  _ct.c_size_t,
]
lib.octio_set_fda_fundus_jpeg.restype = _ct.c_int
lib.octio_save.argtypes = [_ct.c_void_p, _ct.c_char_p]
lib.octio_save.restype = _ct.c_int


def check(status: int) -> None:
  """Raise :class:`RuntimeError` when a native call fails.

  Parameters
  ----------
  status : int
    Native status code, where zero indicates success.

  Raises
  ------
  RuntimeError
    If ``status`` is non-zero.
  """

  if status != 0:
    msg = lib.octio_last_error()
    raise RuntimeError(msg.decode("utf-8", "replace") if msg else "octio native call failed")
