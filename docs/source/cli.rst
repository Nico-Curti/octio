Command line
============

Installing the Python package provides both ``octio`` and the compatibility
``octio-inspect`` entry point.

Inspect a file::

  octio inspect scan.fda
  octio inspect scan.e2e --json

Print decoded metadata::

  octio metadata scan.fda

Inspect native chunks/data elements::

  octio chunks scan.fda

Lossless copy::

  octio copy source.fda destination.fda

Batch metadata indexing::

  octio batch /data/oct --recursive --workers 8 --output metadata.csv

The repository also provides functionally equivalent Bash and PowerShell quick
build scripts in ``scripts/build_local.sh`` and ``scripts/build_local.ps1``.
