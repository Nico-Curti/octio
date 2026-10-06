Supported formats
=================

Topcon FDA
----------

The FDA backend indexes the native length-prefixed chunk stream without reading
large image payloads into RAM. Supported high-level structures include:

* ``PATIENT_INFO_03`` patient metadata;
* acquisition/laterality and device metadata;
* raw ``IMG_MOT_COMP_03`` OCT volumes;
* ``IMG_JPEG`` compressed B-scans as an optional Python/Pillow fallback;
* ``IMG_FUNDUS`` and ``IMG_TRC_02`` localizer/fundus images;
* ``CONTOUR_INFO`` retinal layer maps;
* complete low-level access to every indexed FDA chunk.

Unrecognized chunks remain available through ``Document::chunk_bytes`` and
``OCTFile.chunk_view`` so reverse-engineering work does not require parser
changes before inspecting raw structures.

Heidelberg E2E
--------------

The E2E backend exposes the same common object model for patient metadata, OCT
slices, localizer/fundus data and contour elements. E2E remains a proprietary,
reverse-engineered format, so the writer is intentionally conservative:
unknown structures are preserved and native body patches must not change size.

Unified object model
--------------------

Both backends expose:

* patient, acquisition and device metadata;
* a native chunk/data-element index;
* one or more logical OCT volumes;
* fundus/localizer images;
* segmentation boundaries/maps;
* low-level raw byte access;
* native-format save operations.
