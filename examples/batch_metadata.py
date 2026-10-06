"""Extract common metadata from FDA/E2E files without materializing images."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path

import octio


def main() -> int:
  """Index a directory recursively and write one CSV row per OCT file."""
  parser = argparse.ArgumentParser()
  parser.add_argument("root", type=Path)
  parser.add_argument("output", type=Path)
  args = parser.parse_args()

  paths = sorted(
    p for p in args.root.rglob("*")
    if p.is_file() and p.suffix.lower() in {".fda", ".e2e"}
  )

  fields = [
    "path", "format", "patient_id", "first_name", "last_name",
    "sex", "birth_date", "datetime", "laterality", "volumes",
    "fundus", "segmentations", "chunks",
  ]
  with args.output.open("w", newline="", encoding="utf-8") as stream:
    writer = csv.DictWriter(stream, fieldnames=fields)
    writer.writeheader()
    for path in paths:
      try:
        with octio.open_file(path) as document:
          metadata = document.metadata
          patient = metadata["patient"]
          acquisition = metadata["acquisition"]
          writer.writerow({
            "path": str(path),
            "format": document.format,
            "patient_id": patient["patient_id"],
            "first_name": patient["first_name"],
            "last_name": patient["last_name"],
            "sex": patient["sex"],
            "birth_date": patient["birth_date"],
            "datetime": acquisition["datetime"],
            "laterality": acquisition["laterality"],
            "volumes": len(document.volumes),
            "fundus": len(document.fundus),
            "segmentations": len(document.segmentations),
            "chunks": len(document.chunks),
          })
      except Exception as exc:
        print(f"Skipping {path}: {exc}")

  return 0


if __name__ == "__main__":
  raise SystemExit(main())
