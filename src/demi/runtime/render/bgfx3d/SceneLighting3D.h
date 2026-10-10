#pragma once

#include "demi/runtime/scene/model/World.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime::render {

enum class SceneLightKind3D { Point, Spot, Directional };
struct SceneLight3D {
  SceneLightKind3D kind = SceneLightKind3D::Point;
  Vec3 position{};
  float range = 0;
  Color color{1, 1, 1, 1};
  float intensity = 1;
  Vec3 direction{0, -1, 0};
  float outerConeCos = 0;
  float innerConeCos = 0;
};

struct SceneLighting3D {
  int msaaSamples = 4;
  bool castsShadows = false;
  int shadowResolution = 1024;
  int shadowCascades = 4;
  float shadowDistance = 80.F;
  float shadowSplitLambda = .85F;
  float shadowBlend = .1F;
  std::string shadowFilter = "pcf";
  float shadowBias = .005F;
  int shadowBudget = 1;
  std::string skyTexture;
  std::size_t reliefCacheMeshes = 256;
  std::size_t reliefImageCacheBytes = 64U * 1024U * 1024U;
  std::array<float, 4> direction{-0.4F, -1.0F, -0.3F, 0.0F};
  std::array<float, 4> directionalColor{1.0F, 1.0F, 1.0F, 1.0F};
  std::array<float, 4> ambient{1.0F, 1.0F, 1.0F, 1.0F};
  // The primary directional light remains separate for cascaded shadows.
  std::vector<SceneLight3D> lights;

  [[nodiscard]] bool hasAdditionalLights() const { return !lights.empty(); }
};

[[nodiscard]] SceneLighting3D
collectSceneLighting3D(const World &world, std::string_view renderMask);

} // namespace demi::runtime::render
