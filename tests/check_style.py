"""Repository style and Python docstring checks used by CTest and CI."""

from __future__ import annotations

import ast
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SOURCE_SUFFIXES = {".cpp", ".hpp", ".h", ".py"}
SKIP_PARTS = {"build", "__pycache__", ".git", ".venv", "venv"}
MAX_CODE_LINE = 100


def source_files() -> list[Path]:
  """Return source files covered by the repository style policy."""
  files: list[Path] = []
  for path in ROOT.rglob("*"):
    if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
      continue
    if any(
      part in SKIP_PARTS or part.startswith("build")
      for part in path.relative_to(ROOT).parts
    ):
      continue
    files.append(path)
  return sorted(files)


def check_text_style(path: Path) -> list[str]:
  """Validate whitespace, indentation and source line length for one file."""
  errors: list[str] = []
  for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
    prefix = f"{path.relative_to(ROOT)}:{number}"
    if "\t" in line:
      errors.append(f"{prefix}: tab character is not allowed")
    if line.rstrip() != line:
      errors.append(f"{prefix}: trailing whitespace")
    if len(line) > MAX_CODE_LINE:
      errors.append(f"{prefix}: line exceeds {MAX_CODE_LINE} characters")
    stripped = line.lstrip(" ")
    if stripped and not stripped.startswith(("#", "*", "*/")):
      indent = len(line) - len(stripped)
      if indent % 2:
        errors.append(f"{prefix}: indentation is not a multiple of two spaces")
  return errors


def check_python_docstrings(path: Path) -> list[str]:
  """Require docstrings on Python modules, classes and named functions."""
  tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
  errors: list[str] = []
  if ast.get_docstring(tree) is None:
    errors.append(f"{path.relative_to(ROOT)}: module docstring is missing")
  for node in ast.walk(tree):
    if isinstance(node, (ast.ClassDef, ast.FunctionDef, ast.AsyncFunctionDef)):
      if node.name.startswith("__") and node.name.endswith("__"):
        continue
      if ast.get_docstring(node) is None:
        errors.append(
          f"{path.relative_to(ROOT)}:{node.lineno}: "
          f"docstring is missing for {node.name}"
        )
  return errors


def main() -> int:
  """Run all repository style checks and return a shell-compatible status."""
  errors: list[str] = []
  for path in source_files():
    errors.extend(check_text_style(path))
    if path.suffix == ".py":
      errors.extend(check_python_docstrings(path))
  if errors:
    print("\n".join(errors))
    return 1
  print("Style and Python docstring checks passed.")
  return 0


if __name__ == "__main__":
  raise SystemExit(main())
