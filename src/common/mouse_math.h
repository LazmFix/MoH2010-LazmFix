#pragma once

#include <cmath>
#include <cstdint>

namespace mohpcf {

// Multiplies mouse counts by `factor` without losing fractions: the part that does not make a whole
// count stays in `accumulator` for the next read. factor 1 returns the counts unchanged.
inline std::int32_t scale_axis(std::int32_t value, float factor, double& accumulator) {
  if (factor == 1.0F) {
    return value;
  }
  accumulator += static_cast<double>(value) * factor;
  const double whole = std::trunc(accumulator);
  accumulator -= whole;
  return static_cast<std::int32_t>(whole);
}

}  // namespace mohpcf
