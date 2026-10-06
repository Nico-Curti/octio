"""High-performance FDA/E2E ophthalmic OCT I/O.

The public Python API mirrors the format-independent C++ :class:`Document`
interface while keeping the native implementation behind a small stable C ABI.
"""

from .api import FundusImage, OCTFile, Segmentation, Volume, open_file

__all__ = ["OCTFile", "Volume", "FundusImage", "Segmentation", "open_file"]
__version__ = "0.2.2"
