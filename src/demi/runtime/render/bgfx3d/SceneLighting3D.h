#pragma once

#include "demi/runtime/scene/model/World.h"

#include <array>
#include <string_view>

namespace demi::runtime::render {

struct SceneLighting3D {
  std::array<float, 4> direction{-0.4F, -1.0F, -0.3F, 0.0F};
  std::array<float, 4> directionalColor{1.0F, 1.0F, 1.0F, 1.0F};
  std::array<float, 4> ambient{1.0F, 1.0F, 1.0F, 1.0F};
  std::array<float, 16> pointPositionRange{};
  std::array<float, 16> pointColorIntensity{};
  std::array<float, 16> spotPositionRange{};
  std::array<float, 16> spotDirectionOuter{};
  std::array<float, 16> spotColorIntensity{};
  std::array<float, 16> spotInner{};
};

[[nodiscard]] SceneLighting3D
collectSceneLighting3D(const World &world, std::string_view renderMask);

} // namespace demi::runtime::render
