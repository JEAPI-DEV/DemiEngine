#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace demi::runtime::render {

[[nodiscard]] inline std::uint8_t colorChannelRgba8(const float value) {
  return static_cast<std::uint8_t>(
      std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
}

// bgfx reads a normalized vertex color from its in-memory RGBA byte order.
[[nodiscard]] inline std::uint32_t packVertexColorRgba8(const Color &color) {
  return static_cast<std::uint32_t>(colorChannelRgba8(color.r)) |
         (static_cast<std::uint32_t>(colorChannelRgba8(color.g)) << 8U) |
         (static_cast<std::uint32_t>(colorChannelRgba8(color.b)) << 16U) |
         (static_cast<std::uint32_t>(colorChannelRgba8(color.a)) << 24U);
}

// bgfx view-clear APIs interpret the integer itself as 0xRRGGBBAA.
[[nodiscard]] inline std::uint32_t packClearColorRgba8(const Color &color) {
  return (static_cast<std::uint32_t>(colorChannelRgba8(color.r)) << 24U) |
         (static_cast<std::uint32_t>(colorChannelRgba8(color.g)) << 16U) |
         (static_cast<std::uint32_t>(colorChannelRgba8(color.b)) << 8U) |
         static_cast<std::uint32_t>(colorChannelRgba8(color.a));
}

} // namespace demi::runtime::render
