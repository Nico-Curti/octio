Writing and round-trip safety
=============================

FDA
---

FDA writing is template-based. The writer starts from a valid FDA, preserves
unknown chunks and opaque trailers, replaces only requested payloads and writes
updated chunk lengths. Supported high-level mutations include patient metadata,
raw OCT volumes, segmentation layers and JPEG fundus data.

.. code-block:: python

  import octio

  with octio.open_file("source.fda") as document:
    document.set_patient(first_name="Example", last_name="Patient")
    document.save("edited.fda")

For unchanged real Triton files, the development validation includes
byte-identical round-trip output. This does not imply that arbitrary files built
from scratch or arbitrary cross-chunk geometry changes are accepted by every
Topcon software release.

E2E
---

E2E directory entries use absolute offsets. To avoid silently corrupting the
container, generic raw replacement is restricted to the original element size.
The safe path is lossless copy plus fixed-size patching.

Zero-copy lifetime
------------------

Any NumPy/memory views obtained before ``save`` still refer to the original
memory mapping. Reopen the destination after writing if subsequent work should
use newly written bytes.
