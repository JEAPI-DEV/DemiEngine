#pragma once

#include "demi/runtime/render/bgfx3d/OpaqueInstancePlan3D.h"
#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime::render {

// Explicit scratch for one immutable camera's depth/color pair. Reset before
// the next camera or presentation; only allocation capacity survives reset.
// Terrain selection and pass-local fallback records never enter these slices.
class PreparedInstanceView3D {
public:
  struct Source {
    SceneVisibility3D visibility;
    bool ready = false;
  };

  void reset();
  void beginView(const World &world, const BgfxCameraFrame3D &frame,
                 Vec3 lodPosition);
  Source &source(const Entity &entity);
  [[nodiscard]] std::size_t capacityHint() const;
  [[nodiscard]] OpaqueInstancePlan3D &opaquePlan() noexcept {
    return opaquePlan_;
  }

private:
  struct Key {
    const World *world = nullptr;
    std::string cameraId;
    std::string renderMask;
    std::array<float, 16> projectionAndPose{};
    std::array<std::uint16_t, 4> viewport{};
    bool perspective = true;
    ShadowCasterHull3D shadowCasterHull;
    bool shadowPass = false;
    bool operator==(const Key &) const = default;
  };

  OpaqueInstancePlan3D opaquePlan_;
  std::optional<Key> key_;
  std::unordered_map<const Entity *, std::size_t> indices_;
  std::vector<Source> sources_;
  std::size_t usedSources_ = 0;
};

} // namespace demi::runtime::render
