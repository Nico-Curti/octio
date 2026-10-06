"""Print the same unified FDA/E2E summary as ``examples/read_oct.cpp``."""

from __future__ import annotations

import argparse
from pathlib import Path

import octio
from octio.cli import summary_lines


def main() -> int:
  """Open one OCT file and print the canonical summary.

  Returns
  -------
  int
    Process exit status.
  """

  parser = argparse.ArgumentParser()
  parser.add_argument("file", type=Path)
  args = parser.parse_args()

  with octio.open_file(args.file) as document:
    print("\n".join(summary_lines(document)))
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
