#include "octio/document.hpp"

#include <cmath>

/** \brief Verify that the installed CMake package links the public API. */
int main() {
  const float value = octio::e2e_ufloat16_to_float(0);
  return std::isfinite(value) ? 0 : 1;
}
