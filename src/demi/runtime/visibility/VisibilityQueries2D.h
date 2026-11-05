#pragma once
#include "demi/runtime/scene/model/SceneTypes.h"
#include <string>
#include <vector>
namespace demi::runtime {
struct World;
struct VisibilityBoundary2D {
  std::vector<float> distances;
  std::vector<float> angles;
};
// Sorted collider hits, refined on both sides of surface transitions.
VisibilityBoundary2D castVisibilityBoundary2D(const World &world, Vec2 focus,
                                              float radius,
                                              const std::string &occluderLayer);
} // namespace demi::runtime
