"""Sphinx configuration for octio documentation."""

from __future__ import annotations

project = "octio"
author = "octio contributors"
release = "0.2.1"
version = release

extensions = [
  "sphinx.ext.autodoc",
  "sphinx.ext.napoleon",
  "sphinx.ext.viewcode",
  "sphinx.ext.autosectionlabel",
]

napoleon_numpy_docstring = True
napoleon_google_docstring = False
autodoc_typehints = "description"
autosectionlabel_prefix_document = True
exclude_patterns = ["_build"]
html_theme = "furo"
