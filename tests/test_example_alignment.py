"""Verify that the C++ and Python examples produce identical summaries."""

from __future__ import annotations

import os
import struct
import subprocess
import sys
import tempfile
from pathlib import Path


def _chunk(name: str, payload: bytes) -> bytes:
  """Serialize one FDA chunk.

  Parameters
  ----------
  name : str
    Chunk identifier.
  payload : bytes
    Chunk body.

  Returns
  -------
  bytes
    Serialized chunk.
  """

  encoded = name.encode("ascii")
  return bytes([len(encoded)]) + encoded + struct.pack("<I", len(payload)) + payload


def _make_fda(path: Path) -> None:
  """Create a minimal FDA fixture for language-example comparison.

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


def main() -> int:
  """Run both examples against one fixture and compare standard output.

  Returns
  -------
  int
    Process exit status.
  """

  if len(sys.argv) != 3:
    raise SystemExit("usage: test_example_alignment.py <cpp-example> <python-example>")

  cpp_example = Path(sys.argv[1])
  python_example = Path(sys.argv[2])

  with tempfile.TemporaryDirectory(prefix="octio_example_") as temporary:
    source = Path(temporary) / "synthetic.fda"
    _make_fda(source)

    cpp = subprocess.run(
      [str(cpp_example), str(source)],
      check=True,
      capture_output=True,
      text=True,
    )
    py = subprocess.run(
      [sys.executable, str(python_example), str(source)],
      check=True,
      capture_output=True,
      text=True,
      env=os.environ.copy(),
    )

  if cpp.stdout != py.stdout:
    print("C++ output:")
    print(cpp.stdout)
    print("Python output:")
    print(py.stdout)
    return 1
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
