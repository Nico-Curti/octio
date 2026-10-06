octio
=====

``octio`` is a high-performance C++17 library and Python package for reading,
inspecting, editing and losslessly rewriting ophthalmic OCT vendor containers.
It currently provides a mature Topcon FDA backend and an experimental
reverse-engineered Heidelberg E2E backend behind the same object model.

The project is designed around five principles:

* memory-map large source files instead of loading them eagerly;
* expose raw vendor structures without hiding unknown data;
* offer a common FDA/E2E API for metadata, OCT volumes, fundus/localizers and
  layer segmentations;
* preserve unknown native structures during round-trip writing;
* keep mandatory dependencies to the C++ standard library and operating-system
  mapping APIs.

.. toctree::
   :maxdepth: 2
   :caption: User guide

   installation
   quickstart
   formats
   metadata_decoding
   writing
   cli
   performance
   limitations

.. toctree::
   :maxdepth: 2
   :caption: API reference

   python_api
   cpp_api
