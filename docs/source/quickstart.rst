Quick start
===========

Python
------

.. code-block:: python

  import octio

  with octio.open_file("scan.fda") as document:
    print(document.format)
    print(document.metadata["patient"])

    volume = document.volumes[0]
    first_bscan = volume.slice(0)
    center_ascan = volume.a_scan(0, first_bscan.shape[1] // 2)

    for image in document.fundus:
      print(image.info())

    for layer in document.segmentations:
      print(layer.layer, layer.as_numpy().shape)

``Volume.slice`` is zero-copy for native uncompressed FDA and E2E pixel arrays.
NumPy is loaded lazily only when array access is requested.

C++
---

.. code-block:: cpp

  #include <octio/document.hpp>
  #include <iostream>

  int main() {
    auto document = octio::Document::open("scan.fda");

    std::cout << document->patient().first_name << "\n";
    std::cout << document->volumes().size() << "\n";
    std::cout << document->segmentations().size() << "\n";
  }

The complete Python and C++ examples in ``examples/read_oct.py`` and
``examples/read_oct.cpp`` emit the same summary fields in the same order.
