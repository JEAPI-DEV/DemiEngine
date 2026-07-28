#pragma once

#include "demi/runtime/render/RenderStatistics.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <raylib.h>

#include <array>

namespace demi::runtime {

struct World;

struct RenderLight3D {
  Vec3 position;
  Vec3 direction{0.0F, -1.0F, 0.0F};
  Color color{1.0F, 1.0F, 1.0F, 1.0F};
  float intensity = 1.0F;
  float range = 1.0F;
  float innerCos = -1.0F;
  float outerCos = -1.0F;
  int type = 0;
  bool castsShadows = false;
};

struct LightingFrame3D {
  static constexpr std::size_t MaxLights = 4;
  Color ambientColor{0.35F, 0.4F, 0.5F, 1.0F};
  float ambientIntensity = 0.5F;
  Color fogColor{0.56F, 0.74F, 0.95F, 1.0F};
  float fogStart = 80.0F;
  float fogEnd = 220.0F;
  std::array<RenderLight3D, MaxLights> lights{};
  int lightCount = 0;
  int shadowLightCount = 0;
};

[[nodiscard]] LightingFrame3D
collectLighting3D(const World &world, const std::string &renderMask,
                  RenderStatistics &statistics);
void applyLighting3D(const LightingFrame3D &lighting, Shader shader,
                     Vec3 cameraPosition);

} // namespace demi::runtime
