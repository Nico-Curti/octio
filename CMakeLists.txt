cmake_minimum_required(VERSION 3.18)

project(
  octio
  VERSION 0.2.1
  DESCRIPTION "High-performance FDA/E2E ophthalmic OCT I/O"
  LANGUAGES CXX
)

option(BUILD_SHARED_LIBS "Build octio as a shared library" ON)
option(OCTIO_BUILD_TESTS "Build C++ and Python tests" ON)
option(OCTIO_BUILD_TOOLS "Build command-line tools" ON)
option(OCTIO_BUILD_EXAMPLES "Build C++ examples" ON)
option(OCTIO_BUILD_DOCS "Add Doxygen documentation target" OFF)
option(OCTIO_ENABLE_LTO "Enable interprocedural optimization in optimized builds" ON)
option(OCTIO_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Native `cmake --install build` must never silently target /usr/local or
# Program Files. Users can still override this with CMAKE_INSTALL_PREFIX or
# `cmake --install build --prefix <path>`.
if(NOT DEFINED SKBUILD_PLATLIB_DIR AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(
    CMAKE_INSTALL_PREFIX
    "${CMAKE_BINARY_DIR}/install"
    CACHE PATH "Local octio installation prefix" FORCE
  )
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

add_library(
  octio
  src/mapped_file.cpp
  src/document.cpp
  src/fda.cpp
  src/e2e.cpp
  src/c_api.cpp
)
add_library(octio::octio ALIAS octio)

target_include_directories(
  octio
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(BUILD_SHARED_LIBS)
  target_compile_definitions(octio PRIVATE OCTIO_BUILDING_LIBRARY)
else()
  target_compile_definitions(octio PUBLIC OCTIO_STATIC_DEFINE)
endif()

if(MSVC)
  target_compile_options(octio PRIVATE /W4 /permissive-)
  if(OCTIO_WARNINGS_AS_ERRORS)
    target_compile_options(octio PRIVATE /WX)
  endif()
  if(BUILD_SHARED_LIBS)
    set_target_properties(octio PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
  endif()
else()
  target_compile_options(octio PRIVATE -Wall -Wextra -Wpedantic)
  if(OCTIO_WARNINGS_AS_ERRORS)
    target_compile_options(octio PRIVATE -Werror)
  endif()
endif()

include(CheckIPOSupported)
if(OCTIO_ENABLE_LTO)
  check_ipo_supported(RESULT OCTIO_IPO_SUPPORTED OUTPUT OCTIO_IPO_ERROR)
  if(OCTIO_IPO_SUPPORTED)
    set_property(TARGET octio PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
    set_property(TARGET octio PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
  endif()
endif()

if(OCTIO_BUILD_TOOLS)
  add_executable(octio_inspect tools/octio_inspect.cpp)
  target_link_libraries(octio_inspect PRIVATE octio::octio)
  if(APPLE)
    set_target_properties(octio_inspect PROPERTIES INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
  elseif(UNIX)
    set_target_properties(octio_inspect PROPERTIES INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
  endif()
endif()

if(OCTIO_BUILD_EXAMPLES)
  add_executable(octio_read_example examples/read_oct.cpp)
  target_link_libraries(octio_read_example PRIVATE octio::octio)
endif()

if(OCTIO_BUILD_TESTS)
  enable_testing()

  add_executable(octio_tests tests/test_main.cpp)
  target_link_libraries(octio_tests PRIVATE octio::octio)
  add_test(NAME octio_cpp_tests COMMAND octio_tests)

  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(
      NAME octio_style
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_style.py
    )
  endif()

  if(BUILD_SHARED_LIBS AND Python3_Interpreter_FOUND)
    add_test(
      NAME octio_python_smoke
      COMMAND
        ${CMAKE_COMMAND} -E env
        "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python"
        "OCTIO_LIBRARY=$<TARGET_FILE:octio>"
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_python.py
    )

    add_test(
      NAME octio_editable_loader
      COMMAND
        ${Python3_EXECUTABLE}
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_editable_loader.py
        ${CMAKE_CURRENT_SOURCE_DIR}/python/octio/_native.py
        $<TARGET_FILE:octio>
    )

    if(OCTIO_BUILD_EXAMPLES)
      add_test(
        NAME octio_example_alignment
        COMMAND
          ${CMAKE_COMMAND} -E env
          "PYTHONPATH=${CMAKE_CURRENT_SOURCE_DIR}/python"
          "OCTIO_LIBRARY=$<TARGET_FILE:octio>"
          ${Python3_EXECUTABLE}
          ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_example_alignment.py
          $<TARGET_FILE:octio_read_example>
          ${CMAKE_CURRENT_SOURCE_DIR}/examples/read_oct.py
      )
    endif()
  endif()
endif()

if(OCTIO_BUILD_DOCS)
  find_package(Doxygen REQUIRED)
  configure_file(docs/Doxyfile.in docs/Doxyfile @ONLY)
  add_custom_target(
    docs-cpp
    COMMAND ${DOXYGEN_EXECUTABLE} ${CMAKE_CURRENT_BINARY_DIR}/docs/Doxyfile
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Building octio C++ API documentation"
    VERBATIM
  )
endif()

# scikit-build-core makes the CMake install prefix point at the wheel/editable
# platlib staging tree. Keep the destination relative so redirect-mode editable
# installs can expose the CMake tree as an additional package search location.
if(DEFINED SKBUILD_PLATLIB_DIR)
  install(
    TARGETS octio
    RUNTIME DESTINATION octio
    LIBRARY DESTINATION octio
    ARCHIVE DESTINATION "${SKBUILD_NULL_DIR}"
  )
else()
  install(
    TARGETS octio
    EXPORT octioTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  )

  if(OCTIO_BUILD_TOOLS)
    install(TARGETS octio_inspect RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  endif()

  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  configure_package_config_file(
    cmake/octioConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/octioConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/octio
  )
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/octioConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
  )

  install(
    EXPORT octioTargets
    FILE octioTargets.cmake
    NAMESPACE octio::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/octio
  )
  install(
    FILES
      ${CMAKE_CURRENT_BINARY_DIR}/octioConfig.cmake
      ${CMAKE_CURRENT_BINARY_DIR}/octioConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/octio
  )
endif()
