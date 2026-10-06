#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build/local"
PREFIX="${BUILD_DIR}/install"
CONFIG="Release"
RUN_TESTS=1
VCPKG_ROOT_ARG="${VCPKG_ROOT:-}"

usage() {
  cat <<USAGE
Usage: scripts/build_local.sh [options]

Options:
  --build-dir PATH    CMake build directory (default: build/local)
  --prefix PATH       Local install prefix (default: <build-dir>/install)
  --config NAME       Build configuration (default: Release)
  --no-tests          Skip CTest
  --vcpkg-root PATH   Use PATH/scripts/buildsystems/vcpkg.cmake
  -h, --help          Show this help
USAGE
}

while (($#)); do
  case "$1" in
    --build-dir)
      BUILD_DIR="$2"
      shift 2
      ;;
    --prefix)
      PREFIX="$2"
      shift 2
      ;;
    --config)
      CONFIG="$2"
      shift 2
      ;;
    --no-tests)
      RUN_TESTS=0
      shift
      ;;
    --vcpkg-root)
      VCPKG_ROOT_ARG="$2"
      shift 2
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

cmake_args=(
  -S "${ROOT_DIR}"
  -B "${BUILD_DIR}"
  -DCMAKE_BUILD_TYPE="${CONFIG}"
  -DCMAKE_INSTALL_PREFIX="${PREFIX}"
  -DOCTIO_BUILD_TESTS=ON
  -DOCTIO_BUILD_TOOLS=ON
)

if [[ -n "${VCPKG_ROOT_ARG}" ]]; then
  cmake_args+=(
    -DCMAKE_TOOLCHAIN_FILE="${VCPKG_ROOT_ARG}/scripts/buildsystems/vcpkg.cmake"
    -DVCPKG_MANIFEST_MODE=ON
  )
fi

cmake "${cmake_args[@]}"
cmake --build "${BUILD_DIR}" --config "${CONFIG}" --parallel

if [[ "${RUN_TESTS}" -eq 1 ]]; then
  ctest --test-dir "${BUILD_DIR}" -C "${CONFIG}" --output-on-failure
fi

cmake --install "${BUILD_DIR}" --config "${CONFIG}" --prefix "${PREFIX}"

echo "octio installed locally to: ${PREFIX}"
