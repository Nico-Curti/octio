#pragma once

/*
 * Public symbol visibility for octio.
 *
 * The library uses explicit exports instead of CMake's
 * WINDOWS_EXPORT_ALL_SYMBOLS. Explicit visibility is more reliable across
 * MSVC/CMake versions and avoids post-processing COFF object files to create
 * a .def file.
 */
#if defined(OCTIO_STATIC_DEFINE)
  #define OCTIO_API
#elif defined(_WIN32) || defined(__CYGWIN__)
  #if defined(OCTIO_BUILDING_LIBRARY)
    #define OCTIO_API __declspec(dllexport)
  #else
    #define OCTIO_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) || defined(__clang__)
  #define OCTIO_API __attribute__((visibility("default")))
#else
  #define OCTIO_API
#endif
