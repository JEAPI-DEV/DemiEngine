#pragma once

#include "demi/runtime/scene/model/World.h"

#include <array>
#include <string>
#include <string_view>

namespace demi::runtime::render {

struct SceneLighting3D {
  int msaaSamples = 4;
  bool castsShadows = false;
  int shadowResolution = 1024;
  float shadowDistance = 80.F, shadowBias = .02F;
  int shadowBudget = 1;
  std::string skyTexture;
  std::size_t reliefCacheMeshes = 256;
  std::size_t reliefImageCacheBytes = 64U*1024U*1024U;
  std::array<float, 4> direction{-0.4F, -1.0F, -0.3F, 0.0F};
  std::array<float, 4> directionalColor{1.0F, 1.0F, 1.0F, 1.0F};
  std::array<float, 4> ambient{1.0F, 1.0F, 1.0F, 1.0F};
  std::array<float, 16> pointPositionRange{};
  std::array<float, 16> pointColorIntensity{};
  std::array<float, 16> spotPositionRange{};
  std::array<float, 16> spotDirectionOuter{};
  std::array<float, 16> spotColorIntensity{};
  std::array<float, 16> spotInner{};

  [[nodiscard]] bool hasLocalLights() const {
    for (std::size_t index = 3; index < 16; index += 4) {
      if ((pointPositionRange[index] > 0 && pointColorIntensity[index] > 0) ||
          (spotPositionRange[index] > 0 && spotColorIntensity[index] > 0))
        return true;
    }
    return false;
  }
};

[[nodiscard]] SceneLighting3D
collectSceneLighting3D(const World &world, std::string_view renderMask);

} // namespace demi::runtime::render
