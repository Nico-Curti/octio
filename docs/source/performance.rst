Performance model
=================

``octio`` is optimized for large clinical archives where indexing and selective
access are more common than eagerly loading every volume.

* One memory mapping is created per open document.
* FDA indexing is linear in the number of chunks, not in image byte count.
* Uncompressed FDA B-scans are exposed directly from the mapped file.
* E2E native pixel codes are exposed directly and converted to ``float32`` only
  on request.
* A-scans are copied only because they are generally strided columns.
* Unknown payloads are not parsed or copied during indexing.
* The C++ core has no mandatory image-codec, JSON, NumPy, HDF5 or OpenCV
  dependency.

For batch metadata work, the Python CLI can process independent files using a
thread pool. Storage throughput typically becomes the limiting factor before
CPU parsing does, so worker count should be tuned to the underlying disk.
