# Validation status

This file records the validation performed for the source snapshot shipped with
this project. The commands below were executed from a clean build directory
unless otherwise stated.

## Native builds

The current source snapshot passed strict Release builds with both GNU and
Clang on Linux:

- GCC 14.2, C++17, `-Wall -Wextra -Wpedantic -Werror`: **PASS**;
- Clang 17, C++17, `-Wall -Wextra -Wpedantic -Werror`: **PASS**;
- shared-library build (`BUILD_SHARED_LIBS=ON`): **PASS**;
- static-library build (`BUILD_SHARED_LIBS=OFF`): **PASS**.

The shared build runs four CTest targets:

1. native C++ FDA/E2E tests;
2. source-format and Python-docstring policy test;
3. Python C-ABI smoke test;
4. strict C++/Python example-output parity test.

All four pass. The static build runs the native and style tests; Python C-ABI
tests are intentionally omitted because `ctypes` requires a shared library.

## Native installation

Both installation modes were verified:

- bare `cmake --install <build>` installs to `<build>/install` by default;
- `cmake --install <build> --prefix <custom>` installs to the requested local
  prefix.

An independent downstream CMake project in `tests/consumer` was configured
against the installed tree and linked successfully using:

```cmake
find_package(octio CONFIG REQUIRED)
target_link_libraries(octio_consumer PRIVATE octio::octio)
```

The consumer test passed for both shared and static installed libraries.

## Python/scikit-build-core layout

The container used for this validation is intentionally offline and does not
have `scikit-build-core` preinstalled, so a real PEP 517 wheel build could not
be executed locally without downloading the build backend.

The exact CMake staging contract used by scikit-build-core was nevertheless
verified by configuring with `SKBUILD_PLATLIB_DIR` and `SKBUILD_NULL_DIR`,
building the shared C++ backend, and staging the package exactly as a wheel does.
The resulting clean virtual environment imported `octio` with no
`PYTHONPATH`, `OCTIO_LIBRARY`, `LD_LIBRARY_PATH` or equivalent setting. The
native library resolved from the installed `octio` package directory.

GitHub Actions performs the actual `pip install ".[test]"` flow on Linux,
macOS and Windows with Python 3.10, 3.12 and 3.14, and checks that the native
library is installed beside the Python sources.

## Local helper scripts

`scripts/build_local.sh` was executed end-to-end with a custom build directory
and custom local prefix. Configure, build, CTest and install all passed.
The PowerShell script follows the same argument and operation sequence; its
execution is covered by Windows CI because PowerShell is not available in the
local Linux validation container.

## vcpkg

The native target honors `BUILD_SHARED_LIBS`, which is required for vcpkg
static and dynamic triplets. The manifest and overlay-port JSON files parse
successfully, and both linkage modes build successfully with the same CMake
project.

The validation container does not include a vcpkg checkout and has no network
access, so the overlay port is exercised by the GitHub Actions vcpkg matrix on
Linux, macOS and Windows. The workflow tests both manifest-mode configuration
and the bundled overlay port.

## Real Topcon FDA validation

The reader was exercised against 11 available Topcon DRI OCT Triton FDA files.
The previously measured median native open/index time in this environment was
about 4.04 ms (range about 0.34--10.31 ms). This is an indexing benchmark, not
a full-volume materialization benchmark; the design intentionally avoids
touching image payload pages until requested.

For one representative real Triton validation file, the current parser returns:

- decoded patient metadata and laterality;
- one raw OCT volume, 9 B-scans, each 992 x 512 uint16;
- one 640 x 480 JPEG localizer/fundus image;
- 10 retinal segmentation maps;
- 35 native FDA chunks.

The C++ and Python examples produce byte-for-byte identical textual summaries
for this real FDA file.

An unmodified FDA save was revalidated after the packaging/build refactor and
remains byte-identical to the input. Both files have SHA-256:

`8f9b78ad789969b47f292ddb58037225c2e5eef069d28ab6d7868d930321b548`

Earlier mutation/reopen validation changed patient metadata, one OCT voxel, one
ILM value and the fundus JPEG payload; all edited values were verified after
reopening the saved FDA.

A real FDA with `IMG_MOT_COMP_03` removed was also used to exercise the
`IMG_JPEG` fallback. `Volume.slice()`, `slice_float32()` and `a_scan()` decoded
correctly through Pillow while the C++ core remained codec-free.

## E2E validation limitation

No real E2E file was available in this environment. The E2E backend is covered
by synthetic directory, image and contour fixtures derived from public
reverse-engineering work. Native E2E writing remains deliberately restricted
to same-size body patches so absolute directory offsets are preserved.

Representative real-file validation against HEYEX/E2E data is still required
before the E2E writer should be considered production or clinical-grade.

## Documentation and CI configuration

The Sphinx/ReadTheDocs and GitHub Actions YAML files parse successfully. Local
Sphinx/Doxygen rendering was not run because Sphinx, Furo and Doxygen are not
installed in the offline validation container. The dedicated documentation CI
and ReadTheDocs configuration install those dependencies and build with Sphinx
warnings treated as errors.

## Vendor compatibility scope

FDA writing is template-based. Unknown chunks and opaque trailer bytes are
preserved. Byte-identical unmodified round-trip is verified on real Triton data.
This does **not** prove that arbitrary geometry changes or files synthesized
from scratch are accepted by every Topcon software release; official vendor
specifications are not available.

## Editable Python installation regression

Version 0.2.3 adds an explicit regression test for scikit-build-core editable
installs. The Python module may be imported from the live source tree while the
shared library is present only in a second CMake install-tree package path. The
loader searches ``octio.__path__`` and scikit-build-core loader paths before
falling back to the platform dynamic loader. This removes the need for
``PYTHONPATH`` or ``OCTIO_LIBRARY`` with ``pip install -e .``.

The CMake install destination used for Python builds is relative ``octio/`` as
recommended for ctypes packages by scikit-build-core, so both regular wheels
and redirect-mode editables expose the compiled library in the package search
path.

## Windows explicit symbol export regression

Version 0.2.3 removes CMake `WINDOWS_EXPORT_ALL_SYMBOLS` and exports the public
C and C++ APIs explicitly through `octio/export.hpp`. This avoids CMake's
`cmake -E __create_def` COFF post-processing path, which is not reliable with
all combinations of recent CMake and MSVC/Visual Studio releases. Shared-library
tests on GCC/Clang use hidden-by-default visibility so missing `OCTIO_API`
annotations are detected during ordinary Linux/macOS builds as well.

The installed-package consumer now calls `e2e_ufloat16_to_float()` instead of
checking only a header enum, so CI verifies real shared-library linkage.

## CI isolation fixes in 0.2.3

The Windows editable-loader regression test imports the native DLL in a child
Python process. Windows keeps loaded DLL files locked until the loading process
terminates, so the parent process waits for the child before deleting the
temporary installation tree.

The repository style checker scans only project-owned source roots. In
particular, CI-created trees such as `.vcpkg/` are excluded by construction and
third-party source formatting can never affect octio style results.

`OCTIO_BUILD_PYTHON_TESTS` can disable Python/native-loader integration tests
for native package-manager builds. The vcpkg workflow uses this option because
Python packaging is validated independently by the Python CI matrix.
