Installation
============

Python package
--------------

The Python distribution uses ``scikit-build-core``. A normal pip installation
configures and compiles the C++ backend and places the resulting shared library
inside the installed ``octio`` package::

  python -m pip install .

No ``PYTHONPATH`` or platform library-path variable is required after
installation. Optional array/image functionality can be installed with::

  python -m pip install ".[all]"

For editable development installs::

  python -m pip install -e ".[test]"

Editable installs use scikit-build-core's redirect mode. The Python sources are
read directly from ``python/octio`` while the native shared library remains in
the CMake editable install tree. ``octio`` searches both package locations
automatically, so neither ``PYTHONPATH`` nor ``OCTIO_LIBRARY`` is required.

Native CMake build
------------------

The default CMake install prefix is deliberately local to the build directory::

  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build --parallel
  ctest --test-dir build --output-on-failure
  cmake --install build

The last command installs to ``build/install`` unless a prefix was explicitly
configured. Override it without administrator privileges using either::

  cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/path/to/local/prefix

or::

  cmake --install build --prefix /path/to/local/prefix

A downstream CMake project can then consume the exported target::

  find_package(octio CONFIG REQUIRED)
  target_link_libraries(my_tool PRIVATE octio::octio)

Windows, macOS and Linux
------------------------

The mapping layer uses ``CreateFileMapping``/``MapViewOfFile`` on Windows and
``mmap`` on POSIX systems. CI builds and tests native and Python installations
on Windows, macOS and Linux.

vcpkg
-----

The repository contains both a manifest and an overlay port. With an existing
vcpkg checkout::

  cmake -S . -B build-vcpkg \
    -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
    -DVCPKG_MANIFEST_MODE=ON

or install the project through the bundled overlay port::

  vcpkg install octio --overlay-ports=ports

The core intentionally has no mandatory third-party package dependencies; the
vcpkg integration is primarily provided for consistent toolchain and downstream
package management. The native CMake target honors ``BUILD_SHARED_LIBS``, so
vcpkg static and dynamic triplets are both supported. Python packaging forces a
shared build because the runtime wrapper loads the C ABI with ``ctypes``.
