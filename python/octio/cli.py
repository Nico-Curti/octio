"""Command-line interface for :mod:`octio`.

The module intentionally depends only on the Python standard library unless a
command explicitly materializes image arrays.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Iterable, Sequence

from . import __version__
from .api import OCTFile, open_file


def summary_lines(document: OCTFile) -> list[str]:
  """Return the canonical human-readable document summary.

  Parameters
  ----------
  document : OCTFile
    Open OCT document.

  Returns
  -------
  list of str
    Stable line-oriented summary used by the Python CLI and examples. The C++
    example intentionally emits the same fields in the same order.
  """

  metadata = document.metadata
  patient = metadata.get("patient", {})
  acquisition = metadata.get("acquisition", {})
  lines = [
    f"Format: {document.format}",
    f"Patient: {patient.get('first_name', '')} {patient.get('last_name', '')}".rstrip(),
    f"Patient ID: {patient.get('patient_id', '')}",
    f"DOB: {patient.get('birth_date', '')}",
    f"Laterality: {acquisition.get('laterality', '')}",
    f"Volumes: {len(document.volumes)}",
  ]

  for index, volume in enumerate(document.volumes):
    info = volume.info()
    height, width = info["shape_per_slice"]
    lines.append(
      f"  [{index}] {info['id']} slices={info['num_slices']} "
      f"shape={height}x{width} storage={info['storage']}"
    )

  lines.extend(
    [
      f"Fundus/localizers: {len(document.fundus)}",
      f"Segmentations: {len(document.segmentations)}",
      f"Native chunks: {len(document.chunks)}",
    ]
  )
  return lines


def _inspect(path: Path, as_json: bool = False) -> int:
  """Inspect one OCT file and write a summary to standard output.

  Parameters
  ----------
  path : pathlib.Path
    FDA or E2E input file.
  as_json : bool, default=False
    Emit structured JSON instead of the canonical text summary.

  Returns
  -------
  int
    Process exit status.
  """

  with open_file(path) as document:
    if as_json:
      payload = {
        "format": document.format,
        "metadata": document.metadata,
        "volumes": [volume.info() for volume in document.volumes],
        "fundus": [image.info() for image in document.fundus],
        "segmentations": [layer.info() for layer in document.segmentations],
        "chunk_count": len(document.chunks),
      }
      print(json.dumps(payload, indent=2, ensure_ascii=False))
    else:
      print("\n".join(summary_lines(document)))
  return 0


def _metadata(path: Path, pretty: bool = True) -> int:
  """Print decoded metadata for one OCT file as JSON.

  Parameters
  ----------
  path : pathlib.Path
    FDA or E2E input file.
  pretty : bool, default=True
    Indent JSON output when true.

  Returns
  -------
  int
    Process exit status.
  """

  with open_file(path) as document:
    indent = 2 if pretty else None
    print(json.dumps(document.metadata, indent=indent, ensure_ascii=False))
  return 0


def _chunks(path: Path) -> int:
  """Print the native chunk/data-element index for one file.

  Parameters
  ----------
  path : pathlib.Path
    FDA or E2E input file.

  Returns
  -------
  int
    Process exit status.
  """

  with open_file(path) as document:
    print(json.dumps(document.chunks, indent=2, ensure_ascii=False))
  return 0


def _copy(source: Path, destination: Path) -> int:
  """Save a native-format copy while preserving unknown data.

  Parameters
  ----------
  source : pathlib.Path
    Source FDA/E2E file.
  destination : pathlib.Path
    Destination file.

  Returns
  -------
  int
    Process exit status.
  """

  with open_file(source) as document:
    document.save(destination)
  return 0


def _discover_files(root: Path, recursive: bool) -> list[Path]:
  """Discover FDA/E2E files beneath a path.

  Parameters
  ----------
  root : pathlib.Path
    File or directory to inspect.
  recursive : bool
    Recurse into subdirectories.

  Returns
  -------
  list of pathlib.Path
    Deterministically sorted input files.
  """

  if root.is_file():
    return [root]
  pattern = "**/*" if recursive else "*"
  return sorted(
    path for path in root.glob(pattern)
    if path.is_file() and path.suffix.lower() in {".fda", ".e2e"}
  )


def _metadata_row(path: Path) -> dict[str, str]:
  """Decode common metadata from one file for batch CSV export.

  Parameters
  ----------
  path : pathlib.Path
    Input file.

  Returns
  -------
  dict of str to str
    Flattened common metadata fields plus an error column.
  """

  row = {
    "filename": path.name,
    "path": str(path),
    "format": "",
    "patient_id": "",
    "first_name": "",
    "middle_name": "",
    "last_name": "",
    "sex": "",
    "birth_date": "",
    "datetime": "",
    "laterality": "",
    "device_model": "",
    "device_serial": "",
    "volumes": "",
    "fundus": "",
    "segmentations": "",
    "chunks": "",
    "error": "",
  }
  try:
    with open_file(path) as document:
      metadata = document.metadata
      patient = metadata.get("patient", {})
      acquisition = metadata.get("acquisition", {})
      device = metadata.get("device", {})
      row.update(
        {
          "format": document.format,
          "patient_id": patient.get("patient_id", ""),
          "first_name": patient.get("first_name", ""),
          "middle_name": patient.get("middle_name", ""),
          "last_name": patient.get("last_name", ""),
          "sex": patient.get("sex", ""),
          "birth_date": patient.get("birth_date", ""),
          "datetime": acquisition.get("datetime", ""),
          "laterality": acquisition.get("laterality", ""),
          "device_model": device.get("model", ""),
          "device_serial": device.get("serial", ""),
          "volumes": str(len(document.volumes)),
          "fundus": str(len(document.fundus)),
          "segmentations": str(len(document.segmentations)),
          "chunks": str(len(document.chunks)),
        }
      )
  except Exception as exc:  # CLI batch mode records errors instead of aborting.
    row["error"] = f"{type(exc).__name__}: {exc}"
  return row


def _batch(root: Path, output: Path, recursive: bool, workers: int) -> int:
  """Index metadata from many FDA/E2E files into a CSV file.

  Parameters
  ----------
  root : pathlib.Path
    Input file or directory.
  output : pathlib.Path
    Destination CSV file.
  recursive : bool
    Recurse into subdirectories.
  workers : int
    Number of worker threads used for independent files.

  Returns
  -------
  int
    Process exit status.
  """

  files = _discover_files(root, recursive)
  if not files:
    raise FileNotFoundError(f"No FDA/E2E files found under {root}")

  worker_count = max(1, workers)
  with ThreadPoolExecutor(max_workers=worker_count) as executor:
    rows = list(executor.map(_metadata_row, files))

  output.parent.mkdir(parents=True, exist_ok=True)
  with output.open("w", newline="", encoding="utf-8") as stream:
    writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
    writer.writeheader()
    writer.writerows(rows)

  failed = sum(bool(row["error"]) for row in rows)
  print(f"Indexed {len(rows)} files ({failed} errors) -> {output}")
  return 1 if failed else 0


def build_parser() -> argparse.ArgumentParser:
  """Create the top-level command-line parser.

  Returns
  -------
  argparse.ArgumentParser
    Configured command parser for the ``octio`` entry point.
  """

  parser = argparse.ArgumentParser(
    prog="octio",
    description="Inspect, index and losslessly copy FDA/E2E OCT files.",
  )
  parser.add_argument("--version", action="version", version=f"octio {__version__}")
  subparsers = parser.add_subparsers(dest="command", required=True)

  inspect_parser = subparsers.add_parser("inspect", help="print a concise file summary")
  inspect_parser.add_argument("file", type=Path)
  inspect_parser.add_argument("--json", action="store_true", help="emit structured JSON")

  metadata_parser = subparsers.add_parser("metadata", help="print decoded metadata JSON")
  metadata_parser.add_argument("file", type=Path)
  metadata_parser.add_argument("--compact", action="store_true")

  chunks_parser = subparsers.add_parser("chunks", help="print native chunk/data-element JSON")
  chunks_parser.add_argument("file", type=Path)

  copy_parser = subparsers.add_parser("copy", help="write a lossless native-format copy")
  copy_parser.add_argument("source", type=Path)
  copy_parser.add_argument("destination", type=Path)

  batch_parser = subparsers.add_parser("batch", help="index a directory to CSV")
  batch_parser.add_argument("root", type=Path)
  batch_parser.add_argument("--output", "-o", type=Path, required=True)
  batch_parser.add_argument("--recursive", "-r", action="store_true")
  batch_parser.add_argument("--workers", "-j", type=int, default=1)

  return parser


def main(argv: Sequence[str] | None = None) -> int:
  """Run the installed ``octio`` command-line interface.

  Parameters
  ----------
  argv : sequence of str, optional
    Command arguments excluding the executable name. ``None`` uses
    :data:`sys.argv`.

  Returns
  -------
  int
    Process exit status.
  """

  args = build_parser().parse_args(argv)
  if args.command == "inspect":
    return _inspect(args.file, args.json)
  if args.command == "metadata":
    return _metadata(args.file, not args.compact)
  if args.command == "chunks":
    return _chunks(args.file)
  if args.command == "copy":
    return _copy(args.source, args.destination)
  if args.command == "batch":
    return _batch(args.root, args.output, args.recursive, args.workers)
  raise AssertionError(f"Unhandled command: {args.command}")


def inspect_entrypoint() -> int:
  """Run the compatibility ``octio-inspect`` console entry point.

  Returns
  -------
  int
    Process exit status.

  Notes
  -----
  ``octio-inspect FILE`` is equivalent to ``octio inspect FILE``.
  """

  return main(["inspect", *sys.argv[1:]])


if __name__ == "__main__":
  raise SystemExit(main())
