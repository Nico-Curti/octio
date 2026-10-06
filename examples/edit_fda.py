"""Safely edit a Topcon FDA by using an existing file as the template."""

from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np

import octio


def main() -> int:
  """Modify selected FDA data, save, and reopen the destination."""
  parser = argparse.ArgumentParser()
  parser.add_argument("input")
  parser.add_argument("output")
  args = parser.parse_args()

  output = Path(args.output)
  with octio.open_file(args.input) as document:
    # Example patient edit. Omitted arguments remain unchanged.
    document.set_patient(first_name="Example")

    # Example pixel edit. Materializing the full volume is intentional here
    # because the writer needs an independent contiguous buffer.
    volume = document.volumes[0].as_numpy(decode_e2e=False)
    volume[0, 0, 0] = 0
    document.set_fda_raw_volume(volume)

    # Example ILM edit when present.
    ilm = next((x for x in document.segmentations if x.layer == "ILM"), None)
    if ilm is not None:
      values = ilm.as_numpy(copy=True)
      document.set_fda_segmentation("ILM", values)

    document.save(output)

  # Reopen before inspecting edits because zero-copy views in the original
  # handle remain tied to the original memory mapping.
  with octio.open_file(output) as reopened:
    print(reopened.metadata["patient"])
    print(reopened.volumes[0].info())

  return 0


if __name__ == "__main__":
  raise SystemExit(main())
