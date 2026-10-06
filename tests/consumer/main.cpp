#include "octio/document.hpp"

#include <cstdint>

/** \brief Verify that the installed CMake package exposes the public API. */
int main() {
  const auto format = octio::Format::FDA;
  return format == octio::Format::FDA ? 0 : 1;
}
