Topcon metadata decoding
========================

Scope
-----

Topcon FDA metadata is proprietary. ``octio`` implements the deterministic
layout and XOR de-obfuscation inferred from the validated DRI OCT Triton corpus
used during development. The raw source chunk is always retained and exposed.
A firmware/software generation using another mask may therefore require a
backend update rather than silently trusting decoded text.

``PATIENT_INFO_03`` layout
--------------------------

The observed 615-byte ``PATIENT_INFO_03`` payload follows the same field layout
as the publicly reverse-engineered ``PATIENT_INFO_02`` structure. Relevant
regions include:

=================  =============  =============================================
Field              Byte range      Notes
=================  =============  =============================================
Patient ID         0--31           Fixed-width obfuscated text
First name         32--63          Fixed-width obfuscated text
Last name          64--95          Fixed-width obfuscated text
Middle name        96--103         Fixed-width field
Sex                104             Encoded category
Birth date         105--110        Three little-endian ``uint16`` values
Registration       111 onward      Additional fixed-width/date fields
Last visit         173--178        Date when present
Description        519--582        Free description when present
=================  =============  =============================================

The implementation keeps the full 615-byte modal keystream and explicit
validated prefixes for patient ID, first name and last name. Date and sex fields
are decoded with their independently inferred masks.

Validation strategy
-------------------

The metadata implementation was validated using repeated patient profiles,
known names, field cardinalities, patient-ID/name consistency, valid calendar
dates and real-file round trips. A decoded value should still be treated as
reverse-engineered metadata rather than an official vendor API result.

Raw access
----------

When a decoded field is uncertain, inspect the native payload directly:

.. code-block:: python

  with octio.open_file("scan.fda") as document:
    patient_chunks = [
      chunk for chunk in document.chunks
      if "PATIENT_INFO" in chunk["name"]
    ]
    raw = document.chunk_bytes(patient_chunks[0]["index"])
