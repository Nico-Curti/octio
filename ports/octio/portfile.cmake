vcpkg_cmake_configure(
  SOURCE_PATH "${CURRENT_PORT_DIR}/../.."
  OPTIONS
    -DOCTIO_BUILD_TESTS=OFF
    -DOCTIO_BUILD_TOOLS=OFF
    -DOCTIO_BUILD_DOCS=OFF
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(PACKAGE_NAME octio CONFIG_PATH lib/cmake/octio)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
vcpkg_install_copyright(FILE_LIST "${CURRENT_PORT_DIR}/../../LICENSE")
