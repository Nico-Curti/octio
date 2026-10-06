C++ API
=======

The public native API is defined in:

* ``include/octio/document.hpp`` -- high-level format-independent document API;
* ``include/octio/types.hpp`` -- metadata and zero-copy view structures;
* ``include/octio/c_api.h`` -- stable C ABI used by Python and other languages.

Generate complete Doxygen HTML documentation with::

  cmake -S . -B build-docs -DOCTIO_BUILD_DOCS=ON
  cmake --build build-docs --target docs-cpp

Example
-------

.. code-block:: cpp

  #include <octio/document.hpp>

  auto document = octio::Document::open("scan.fda");
  const auto& patient = document->patient();
  const auto& volumes = document->volumes();
  const auto& layers = document->segmentations();
