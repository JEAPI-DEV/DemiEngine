#pragma once

#include "demi/runtime/render/bgfx3d/BgfxCameraFrame3D.h"

#include <array>
#include <optional>
#include <string>

namespace demi::runtime::render {

inline constexpr std::size_t MaxShadowCascades3D = 4;

struct ShadowCascade3D {
  Vec3 eye;
  Vec3 direction;
  Vec3 up;
  float radius = 0;
  float depthRange = 0;
  float receiverNear = 0;
  float receiverFar = 0;
  std::array<float, 4> x{}, y{}, z{};
};

struct ShadowCascadeLayout3D {
  std::array<ShadowCascade3D, MaxShadowCascades3D> cascades{};
  std::array<float, 4> splits{};
  std::array<float, 4> cameraDepth{};
  int count = 0;
};

// Fits overlapping camera-frustum slices using stable bounding spheres and
// snaps their light-space centres to texels. No GPU state or scene mutation.
[[nodiscard]] std::optional<ShadowCascadeLayout3D>
makeShadowCascadeLayout3D(const SceneLighting3D &lighting,
                          const BgfxCameraFrame3D &camera, std::string &error);

} // namespace demi::runtime::render
