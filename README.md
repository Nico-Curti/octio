| **Authors**  | **Project** |  **Documentation** | **Build Status** | **License** |
|:------------:|:-----------:|:------------------:|:----------------:|:-----------:|
| [**N. Curti**](https://github.com/Nico-Curti) | **OCTIO** <br/> [![Python3](https://img.shields.io/badge/code-Python%203-blue)](https://www.python.org/) <br/> [![C++](https://img.shields.io/badge/code-C++-orange)](https://isocpp.org/) | [![Doxygen Sphinx](https://github.com/Nico-Curti/octio/actions/workflows/docs.yml/badge.svg)](https://github.com/Nico-Curti/octio/actions/workflows/docs.yml) <br/> [![docs](https://readthedocs.org/projects/octio/badge/?version=latest)](https://octio.readthedocs.io/en/latest/?badge=latest) | [![CI](https://github.com/Nico-Curti/octio/actions/workflows/ci.yml/badge.svg)](https://github.com/Nico-Curti/octio/actions/workflows/ci.yml) | [![license](https://img.shields.io/github/license/mashape/apistatus.svg)](https://github.com/Nico-Curti/octio/blob/main/LICENSE) |

[![GitHub pull-requests](https://img.shields.io/github/issues-pr/Nico-Curti/octio.svg?style=plastic)](https://github.com/Nico-Curti/octio/pulls)
[![GitHub issues](https://img.shields.io/github/issues/Nico-Curti/octio.svg?style=plastic)](https://github.com/Nico-Curti/octio/issues)

[![GitHub stars](https://img.shields.io/github/stars/Nico-Curti/octio.svg?label=Stars&style=social)](https://github.com/Nico-Curti/octio/stargazers)
[![GitHub watchers](https://img.shields.io/github/watchers/Nico-Curti/octio.svg?label=Watch&style=social)](https://github.com/Nico-Curti/octio/watchers)

# octio

`octio` is a high-performance C++17 library with a unified Python interface for
ophthalmic OCT vendor containers.

The project currently targets:

- **Topcon FDA**: metadata, OCT volumes, fundus/localizer images, retinal layer
  segmentations, low-level native chunks and conservative template-based FDA
  rewriting.
- **Heidelberg E2E**: metadata, OCT image elements, fundus/localizer data,
  current and legacy contour structures, low-level data elements and
  conservative same-size native patching.

The C++ core has **no mandatory third-party runtime dependencies**. Large files
are memory-mapped and raw image arrays are exposed zero-copy whenever the native
layout permits it. Python uses a small stable C ABI through `ctypes`, so there is
no pybind11 dependency and no CPython ABI dependency in the native library.

> **Format status**
>
> FDA and E2E are proprietary vendor formats. The implementation is based on
> observed files and public reverse-engineering work. FDA support has been
> validated substantially more extensively than E2E support. Writing is designed
> around preserving unknown vendor data rather than pretending the undocumented
> formats are fully specified.

---

## 1. Python installation

The Python package is self-contained from the user's point of view. The C++
backend is compiled by `scikit-build-core` and the resulting shared library is
placed inside the installed `octio` package.

```bash
python -m pip install .
```

Optional array and image support:

```bash
python -m pip install ".[all]"
```

Development installation with tests:

```bash
python -m pip install -e ".[test]"
```

Editable installs are fully supported. With scikit-build-core redirect mode,
the Python modules remain live in ``python/octio`` while the compiled library
resides in the editable CMake install tree. The runtime loader searches both
locations automatically; do **not** set ``PYTHONPATH`` or ``OCTIO_LIBRARY`` for
a normal editable install.

After installation:

```python
import octio

with octio.open_file("scan.fda") as document:
  print(document.format)
  print(document.metadata["patient"])
  print(len(document.volumes))
```

No manual `PYTHONPATH` is required.

The installed package also provides:

```bash
octio --version
octio inspect scan.fda
octio metadata scan.fda
octio chunks scan.fda
octio batch /data/oct -r -j 8 -o metadata.csv
```

`octio-inspect scan.fda` is a compatibility shortcut for
`octio inspect scan.fda`.

---

## 2. Native CMake build and local install

Configure and build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

The project deliberately avoids a system install by default. If the user does
not provide `CMAKE_INSTALL_PREFIX`, CMake sets it to:

```text
<build directory>/install
```

Therefore this is safe and local:

```bash
cmake --install build
```

Choose another local prefix either during configure:

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$PWD/local"
cmake --build build --parallel
cmake --install build
```

or only at install time:

```bash
cmake --install build --prefix "$PWD/local"
```

### Downstream CMake use

After installation:

```cmake
find_package(octio CONFIG REQUIRED)

target_link_libraries(my_reader PRIVATE octio::octio)
```

Configure the consuming project with the local prefix:

```bash
cmake -S consumer -B consumer-build \
  -DCMAKE_PREFIX_PATH=/path/to/octio/local
```

---

## 3. Quick-build scripts

### Bash

```bash
./scripts/build_local.sh
./scripts/build_local.sh --prefix "$PWD/local"
./scripts/build_local.sh --vcpkg-root "$VCPKG_ROOT"
```

### PowerShell

```powershell
./scripts/build_local.ps1
./scripts/build_local.ps1 -PrefixPath ./local
./scripts/build_local.ps1 -VcpkgRoot $env:VCPKG_ROOT
```

The Bash and PowerShell scripts perform the same sequence:

1. configure;
2. build;
3. run tests unless disabled;
4. install to a local prefix.

Quick inspection after Python installation:

```bash
./scripts/octio_inspect.sh scan.fda
```

```powershell
./scripts/octio_inspect.ps1 scan.fda
```

---

## 4. vcpkg

The core does not need external libraries, but vcpkg integration is provided so
`octio` fits naturally into projects that standardize their C++ dependencies on
vcpkg.

### Manifest mode

```bash
cmake -S . -B build-vcpkg \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_MANIFEST_MODE=ON \
  -DCMAKE_BUILD_TYPE=Release
```

A `vcpkg-release` CMake preset is also included:

```bash
cmake --preset vcpkg-release
cmake --build --preset vcpkg-release
ctest --preset vcpkg-release
```

### Bundled overlay port

```bash
vcpkg install octio --overlay-ports=ports
```

The port installs the native library, headers and CMake package configuration.
It follows the vcpkg triplet linkage policy: default dynamic Windows triplets
build a DLL, while static triplets build a static library. The Python wheel
explicitly forces ``BUILD_SHARED_LIBS=ON`` because ``ctypes`` requires a shared
library.

Official vcpkg manifest/toolchain documentation:
<https://learn.microsoft.com/vcpkg/consume/manifest-mode>

---

## 5. Unified data model

FDA and E2E use the same high-level concepts:

```text
OCTFile / octio::Document
├── patient metadata
├── acquisition metadata
├── device metadata
├── chunks / native data elements
├── volumes[]
│   ├── slice(i)
│   ├── slice_float32(i)
│   ├── a_scan(slice, x)
│   └── as_numpy()
├── fundus[]
│   ├── bytes()
│   └── decode()
└── segmentations[]
    ├── layer
    ├── info()
    └── as_numpy()
```

### Python

```python
import octio

with octio.open_file("scan.fda") as document:
  patient = document.metadata["patient"]
  acquisition = document.metadata["acquisition"]
  device = document.metadata["device"]

  print(patient)
  print(acquisition)
  print(device)

  volume = document.volumes[0]
  bscan = volume.slice(0)          # zero-copy when native data is raw
  bscan_f32 = volume.slice_float32(0)
  ascan = volume.a_scan(0, bscan.shape[1] // 2)

  for image in document.fundus:
    print(image.info())

  for segmentation in document.segmentations:
    print(segmentation.layer, segmentation.info())
```

### C++

```cpp
#include <octio/document.hpp>
#include <iostream>

int main() {
  auto document = octio::Document::open("scan.fda");

  const auto& patient = document->patient();
  const auto& volume = document->volumes().front();

  std::cout << patient.first_name << ' ' << patient.last_name << '\n';
  std::cout << volume.slices.size() << '\n';
  std::cout << document->segmentations().size() << '\n';
}
```

The complete examples `examples/read_oct.cpp` and `examples/read_oct.py` emit
the same summary fields in the same order.

---

## 6. Topcon FDA support

### Container indexing

Topcon FDA uses a short header followed by named, length-prefixed chunks.
`octio` indexes the chunk stream without materializing large image payloads.
This makes metadata-only archive scans fast and memory-efficient.

Supported structures include:

- `PATIENT_INFO_03`;
- capture metadata and laterality;
- hardware/software metadata;
- scan parameters;
- raw `IMG_MOT_COMP_03` OCT volumes;
- `IMG_JPEG` compressed B-scans;
- `IMG_FUNDUS` and `IMG_TRC_02` localizers/fundus images;
- `CONTOUR_INFO` retinal layer segmentations;
- every unknown chunk through the low-level chunk API.

### Raw volume access

For native raw FDA volumes, a B-scan view points directly into the memory map:

```python
with octio.open_file("scan.fda") as document:
  volume = document.volumes[0]
  bscan = volume.slice(0)
```

No full-volume copy or transpose is needed for this common access path.
A-scans are generally strided columns, so they are copied only when explicitly
requested.

### JPEG OCT fallback

If a file contains JPEG-compressed B-scans instead of `IMG_MOT_COMP_03`, the
C++ core exposes encoded JPEG bytes and dimensions. Python can decode them with
Pillow through the same `Volume.slice()` API.

### Retinal layers

The FDA backend maps known Topcon `CONTOUR_INFO` identifiers to canonical layer
names where possible, including common boundaries such as:

- `ILM`;
- `RNFL_GCL`;
- `GCL_IPL`;
- `IPL_INL`;
- `INL_OPL`;
- `ELM`;
- `MZ_EZ`;
- `IZ_RPE`;
- `BM`;
- `CSI`.

Unknown layer identifiers remain accessible rather than being discarded.

---

## 7. Topcon patient metadata decoding

`PATIENT_INFO_03` is proprietary. The current decoder was reconstructed from a
large DRI OCT Triton corpus using repeated patient profiles, known patient
fields, field cardinalities, patient-ID/name consistency and valid date
constraints.

The 615-byte payload follows the same field organization observed in the
publicly reverse-engineered `PATIENT_INFO_02` structure:

| Region | Content |
| --- | --- |
| 0-31 | Patient ID |
| 32-63 | First name |
| 64-95 | Last name |
| 96-103 | Middle name |
| 104 | Sex |
| 105-110 | Birth date |
| later fields | Registration, record, last visit, physician/contact data, description, reserved |

The implementation uses deterministic XOR masks for text/date/category fields
observed in the validated Triton family. The full original chunk remains
available through low-level access.

This distinction is important: **decoded metadata is reverse-engineered, not an
official Topcon SDK result**. A new firmware or software family can use another
mask. Do not discard raw data after decoding.

Example low-level inspection:

```python
with octio.open_file("scan.fda") as document:
  for chunk in document.chunks:
    if "PATIENT_INFO" in chunk["name"]:
      raw = document.chunk_bytes(chunk["index"])
      print(chunk, len(raw))
```

Public reverse-engineering reference used for format cross-checking:
https://github.com/marksgraham/OCT-Converter

---

## 8. FDA writing

Writing is intentionally **template/round-trip based**.

The writer:

- starts from a structurally valid source FDA;
- preserves header bytes;
- preserves chunk order;
- preserves unknown chunks;
- preserves documented-payload trailers that are not understood;
- replaces only requested data;
- rewrites chunk lengths when necessary.

High-level Python mutations include:

```python
with octio.open_file("source.fda") as document:
  document.set_patient(
    patient_id="patient01",
    first_name="Example",
    last_name="Patient",
    sex="F",
    birth_date="1970-01-02",
  )

  volume = document.volumes[0].as_numpy(copy=True)
  volume[0, 0, 0] = 0
  document.set_fda_raw_volume(volume)

  ilm = next(layer for layer in document.segmentations if layer.layer == "ILM")
  values = ilm.as_numpy(copy=True)
  document.set_fda_segmentation("ILM", values)

  document.save("edited.fda")
```

For real Triton development samples, an unmodified save was verified
byte-identical to the source and selected metadata/volume/segmentation/fundus
mutations were reopened and checked.

Creating an arbitrary FDA from nothing is **not** claimed to be universally
Topcon-compatible because cross-chunk vendor invariants remain undocumented.

---

## 9. Heidelberg E2E support

The E2E backend exposes the same common concepts:

- patient metadata;
- laterality when available;
- raw OCT image elements;
- Heidelberg custom 16-bit float conversion;
- fundus/localizer image elements;
- current and legacy contour structures;
- raw data-element access.

E2E directory structures contain absolute offsets. Safe writing therefore uses
lossless source copying plus same-size data-element patches. Arbitrary element
resizing or creation of new HEYEX-compatible E2E files is not advertised.

Public reverse-engineering references:

- https://github.com/marksgraham/OCT-Converter
- https://github.com/neurodial/LibE2E

Real-file E2E validation should be performed before production/clinical use.

---

## 10. Low-level access

Python:

```python
with octio.open_file("scan.fda") as document:
  print(document.chunks)

  copied = document.chunk_bytes(3)
  zero_copy = document.chunk_view(3)

  document.replace_chunk(3, copied)
```

C++:

```cpp
auto document = octio::Document::open("scan.fda");
const auto& chunks = document->chunks();
auto bytes = document->chunk_bytes(3);
```

Low-level access is intentionally part of the public interface so unsupported
vendor structures can be investigated without forking the parser.

---

## 11. Python command line

```text
octio inspect FILE [--json]
octio metadata FILE [--compact]
octio chunks FILE
octio copy SOURCE DESTINATION
octio batch ROOT --output metadata.csv [--recursive] [--workers N]
```

Batch mode records per-file errors instead of aborting the entire archive scan.
For spinning disks, use a low worker count. SSD/NVMe archives generally benefit
from moderate parallelism.

---

## 12. Performance design

The implementation is optimized around selective access to large OCT archives:

- one memory mapping per open file;
- O(number of native chunks) FDA indexing;
- no eager traversal of large OCT pixel payloads;
- zero-copy FDA raw B-scans;
- zero-copy E2E native pixel-code B-scans;
- lazy Heidelberg float conversion;
- strided A-scan copy only on request;
- lazy NumPy and Pillow imports;
- no mandatory OpenCV, HDF5, JSON library, Boost, pybind11 or image codec;
- sequential FDA writer that copies unknown data without interpretation.

The development validation on available Topcon Triton files is documented in
`docs/VALIDATION.md`. Performance numbers there are environment-specific and
should not be interpreted as universal benchmarks.

---

## 13. Cross-platform support

The project contains platform-specific mapping implementations for:

- Linux and other POSIX systems through `mmap`;
- macOS through the POSIX implementation;
- Windows through native Win32 file mapping.

GitHub Actions builds and tests:

- Ubuntu;
- macOS;
- Windows;
- Python 3.10, 3.12 and 3.14 installations;
- vcpkg configuration/overlay-port use;
- Sphinx and Doxygen documentation.

See `.github/workflows/ci.yml` and `.github/workflows/docs.yml`.

---

## 14. Documentation

### Sphinx

```bash
python -m pip install ".[docs]"
sphinx-build -W -b html docs/source docs/_build/html
```

Read the Docs configuration is provided in `.readthedocs.yaml`.

### Doxygen

```bash
cmake -S . -B build-docs \
  -DOCTIO_BUILD_DOCS=ON \
  -DOCTIO_BUILD_TESTS=OFF \
  -DOCTIO_BUILD_TOOLS=OFF
cmake --build build-docs --target docs-cpp
```

Public C++ declarations use Doxygen documentation. Python public classes and
functions use NumPy-style docstrings suitable for Sphinx/Napoleon.

---

## 15. Testing

Native test suite:

```bash
cmake -S . -B build -DOCTIO_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Installed Python package test:

```bash
python -m pip install ".[test]"
python -m pytest -q tests/test_installed.py
```

The suite includes synthetic FDA and E2E fixtures, lossless round trips, volume
and contour access, FDA mutations, Python/C ABI smoke tests, a source-style and
docstring policy check, and strict C++/Python example-output parity.

A small downstream CMake project in `tests/consumer/` verifies that the native
install exports a usable `octio::octio` target for both shared and static builds.
Cross-platform GitHub Actions additionally exercise normal `pip install` packaging
and vcpkg on Linux, macOS and Windows.

---

## 16. Project layout

```text
include/octio/        Public C++ and C API headers
src/                  Native FDA/E2E implementation
python/octio/         Installed Python package and CLI
examples/             Aligned C++ and Python examples
scripts/              Bash and PowerShell helper scripts
tests/                Native, Python and install-consumer tests
docs/source/          Sphinx documentation
docs/Doxyfile.in      Doxygen configuration
cmake/                Installed CMake package configuration
ports/octio/          vcpkg overlay port
.github/workflows/    Cross-platform CI and documentation CI
```

---

## 17. Validation and limitations

See [`docs/VALIDATION.md`](docs/VALIDATION.md) for the exact validation scope.

Important limitations:

- native FDA/E2E formats are proprietary;
- FDA metadata de-obfuscation may differ across vendor generations;
- E2E real-file validation is less complete than FDA validation;
- arbitrary FDA synthesis and geometry-changing edits can violate unknown
  vendor invariants;
- this library is not a diagnostic medical device.

For clinical/research archives, keep original vendor files immutable and write
edited output to a new path.

## License

MIT. Vendor file formats, software and trademarks remain the property of their
respective owners.

## Acknowledgments

Thanks goes to all contributors of this project.

### Citation

If you have found `OCTIO` helpful in your research, please consider citing the project

```BibTeX
@misc{OCTIO,
  author = {Nico Curti},
  title = {OCTIO},
  year = {2026},
  publisher = {GitHub},
  howpublished = {\url{https://github.com/Nico-Curti/octio}},
}
```
