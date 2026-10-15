#pragma once

#include <cmath>

namespace demi::runtime::render {

// RGB transfer functions only. Alpha and material data channels remain linear;
// callers own validation and any output-range clamping.
inline double decodeSrgbChannel(double value) {
  return value <= 0.04045 ? value / 12.92
                          : std::pow((value + 0.055) / 1.055, 2.4);
}

inline double encodeSrgbChannel(double value) {
  return value <= 0.0031308 ? value * 12.92
                            : 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

} // namespace demi::runtime::render
