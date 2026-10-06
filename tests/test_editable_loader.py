"""Exercise native loading from a scikit-build-core-style editable install."""

from __future__ import annotations

import importlib.util
import shutil
import sys
import tempfile
import types
from pathlib import Path


def main() -> int:
  """Load ``_native.py`` when the shared library exists only in install tree."""
  if len(sys.argv) != 3:
    raise SystemExit("usage: test_editable_loader.py SOURCE_NATIVE NATIVE_LIBRARY")

  native_source = Path(sys.argv[1]).resolve()
  native_library = Path(sys.argv[2]).resolve()

  with tempfile.TemporaryDirectory(prefix="octio_editable_") as tmp:
    install_package = Path(tmp) / "install" / "octio"
    install_package.mkdir(parents=True)
    installed_library = install_package / native_library.name
    shutil.copy2(native_library, installed_library)

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
    assert install_package.resolve() in directories
    assert Path(module.lib._name).resolve() == installed_library.resolve()

  print("Editable native-loader test passed")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
