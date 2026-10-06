"""Smoke tests for the pure-Python ctypes wrapper.

The test intentionally avoids NumPy/Pillow so the wrapper itself remains
usable with only the Python standard library plus the compiled octio library.
"""

from __future__ import annotations

import hashlib
import struct
import tempfile
from pathlib import Path

import octio


def _chunk(name: str, payload: bytes) -> bytes:
  """Build one FDA length-prefixed chunk."""
  encoded = name.encode("ascii")
  return bytes([len(encoded)]) + encoded + struct.pack("<I", len(payload)) + payload


def _make_fda(path: Path) -> None:
  """Create a minimal FDA containing one raw 2x3x2 uint16 volume."""
  header = b"FOCTFDA" + struct.pack("<II", 2, 0)
  payload = bytearray(22 + 2 * 3 * 2 * 2)
  payload[0] = 0
  struct.pack_into("<IIII", payload, 1, 2, 3, 16, 2)
  payload[17] = 0
  struct.pack_into("<I", payload, 18, 24)
  struct.pack_into("<12H", payload, 22, *range(12))
  path.write_bytes(header + _chunk("@IMG_MOT_COMP_03", payload) + b"\x00")


def _sha256(path: Path) -> str:
  """Return a file SHA-256 digest."""
  return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> int:
  """Run build-tree Python API and lossless-save smoke tests."""
  with tempfile.TemporaryDirectory(prefix="octio_python_") as tmp:
    src = Path(tmp) / "synthetic.fda"
    dst = Path(tmp) / "roundtrip.fda"
    _make_fda(src)
    with octio.open_file(src) as document:
      assert document.format == "FDA"
      assert len(document.volumes) == 1
      assert document.volumes[0].num_slices == 2
      assert document.volumes[0].info()["shape_per_slice"] == (3, 2)
      assert len(document.chunks) == 1
      document.save(dst)
    assert _sha256(src) == _sha256(dst)
  print("Python octio smoke test passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
