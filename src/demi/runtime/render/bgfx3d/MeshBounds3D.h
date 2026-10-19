#pragma once
#include "demi/runtime/scene/model/SceneTypes.h"
#include <algorithm>
#include <optional>
#include <span>

namespace demi::runtime::render {
struct MeshBounds3D {
  Vec3 minimum;
  Vec3 maximum;
};

// Compute once when immutable geometry is loaded, never once per camera/copy.
inline std::optional<MeshBounds3D>
meshBounds3D(std::span<const Vec3> positions) {
  if (positions.empty()) {
    return std::nullopt;
  }
  MeshBounds3D bounds{positions.front(), positions.front()};
  for (const auto position : positions.subspan(1)) {
    bounds.minimum = {std::min(bounds.minimum.x, position.x),
                      std::min(bounds.minimum.y, position.y),
                      std::min(bounds.minimum.z, position.z)};
    bounds.maximum = {std::max(bounds.maximum.x, position.x),
                      std::max(bounds.maximum.y, position.y),
                      std::max(bounds.maximum.z, position.z)};
  }
  return bounds;
}
} // namespace demi::runtime::render
