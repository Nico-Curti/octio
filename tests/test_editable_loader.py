"""Exercise native loading from a scikit-build-core-style editable install."""

from __future__ import annotations

import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


_CHILD = r'''\
import importlib.util
import sys
import types
from pathlib import Path

native_source = Path(sys.argv[1]).resolve()
install_package = Path(sys.argv[2]).resolve()
installed_library = Path(sys.argv[3]).resolve()

fake_package = types.ModuleType("octio")
fake_package.__path__ = [
  str(install_package),
  str(native_source.parent),
]
fake_package.__package__ = "octio"
sys.modules["octio"] = fake_package

spec = importlib.util.spec_from_file_location("octio._native", native_source)
if spec is None or spec.loader is None:
  raise RuntimeError("could not create import specification")

module = importlib.util.module_from_spec(spec)
sys.modules["octio._native"] = module
spec.loader.exec_module(module)

directories = module._package_search_dirs()
assert install_package in directories
assert Path(module.lib._name).resolve() == installed_library
print("Editable native-loader child test passed")
'''


def main() -> int:
  """Validate editable loading in a child process that can release the DLL.

  Windows keeps a loaded DLL locked until the loading process terminates.
  Performing the import in a child process guarantees that the native library
  is unloaded before ``TemporaryDirectory`` attempts cleanup.
  """
  if len(sys.argv) != 3:
    raise SystemExit("usage: test_editable_loader.py SOURCE_NATIVE NATIVE_LIBRARY")

  native_source = Path(sys.argv[1]).resolve()
  native_library = Path(sys.argv[2]).resolve()

  with tempfile.TemporaryDirectory(prefix="octio_editable_") as tmp:
    install_package = Path(tmp) / "install" / "octio"
    install_package.mkdir(parents=True)
    installed_library = install_package / native_library.name
    shutil.copy2(native_library, installed_library)

    completed = subprocess.run(
      [
        sys.executable,
        "-c",
        _CHILD,
        str(native_source),
        str(install_package.resolve()),
        str(installed_library.resolve()),
      ],
      check=False,
      capture_output=True,
      text=True,
    )
    if completed.stdout:
      print(completed.stdout, end="")
    if completed.stderr:
      print(completed.stderr, file=sys.stderr, end="")
    if completed.returncode != 0:
      return completed.returncode

  print("Editable native-loader test passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
