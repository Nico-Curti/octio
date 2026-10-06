Limitations and safety notes
============================

* FDA and E2E are proprietary formats reconstructed from observed files and
  public reverse-engineering work.
* ``PATIENT_INFO_03`` masks are validated on the development Triton corpus but
  may vary across firmware/software families.
* E2E real-file validation remains less complete than FDA validation.
* The FDA writer is intended for template-based editing, not unrestricted
  vendor-format synthesis from an empty document.
* Geometry edits can make unrelated vendor registration/calibration chunks
  semantically inconsistent even when the container remains structurally valid.
* The library is not a diagnostic medical device and does not replace vendor
  software validation.
