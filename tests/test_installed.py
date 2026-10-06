"""Tests intended to run against an installed scikit-build-core wheel."""

from __future__ import annotations

import hashlib
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

import octio


def _chunk(name: str, payload: bytes) -> bytes:
  """Build one length-prefixed FDA chunk.

  Parameters
  ----------
  name : str
    Native FDA chunk name.
  payload : bytes
    Chunk payload.

  Returns
  -------
  bytes
    Serialized FDA chunk.
  """

  encoded = name.encode("ascii")
  return bytes([len(encoded)]) + encoded + struct.pack("<I", len(payload)) + payload


def _make_fda(path: Path) -> None:
  """Create a minimal synthetic FDA file.

  Parameters
  ----------
  path : pathlib.Path
    Destination filename.
  """

  header = b"FOCTFDA" + struct.pack("<II", 2, 0)
  payload = bytearray(22 + 2 * 3 * 2 * 2)
  payload[0] = 0
  struct.pack_into("<IIII", payload, 1, 2, 3, 16, 2)
  payload[17] = 0
  struct.pack_into("<I", payload, 18, 24)
  struct.pack_into("<12H", payload, 22, *range(12))
  path.write_bytes(header + _chunk("@IMG_MOT_COMP_03", payload) + b"\x00")


def _sha256(path: Path) -> str:
  """Return the SHA-256 digest of a file.

  Parameters
  ----------
  path : pathlib.Path
    File to hash.

  Returns
  -------
  str
    Lower-case hexadecimal digest.
  """

  return hashlib.sha256(path.read_bytes()).hexdigest()


def test_installed_python_package_and_cli() -> None:
  """Verify normal package import, native library loading and CLI execution."""

  with tempfile.TemporaryDirectory(prefix="octio_installed_") as temporary:
    source = Path(temporary) / "source.fda"
    destination = Path(temporary) / "copy.fda"
    _make_fda(source)

    with octio.open_file(source) as document:
      assert document.format == "FDA"
      assert document.volumes[0].num_slices == 2
      document.save(destination)

    assert _sha256(source) == _sha256(destination)

    result = subprocess.run(
      [sys.executable, "-m", "octio.cli", "inspect", str(source)],
      check=True,
      capture_output=True,
      text=True,
    )
    assert "Format: FDA" in result.stdout
    assert "Volumes: 1" in result.stdout
