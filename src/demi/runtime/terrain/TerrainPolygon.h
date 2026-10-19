#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include <span>
#include <stop_token>

namespace demi::runtime {
// Finite simple polygon, either winding. A repeated closing point is accepted;
// zero-length edges, self-intersections and zero area are diagnosed.
void validateTerrainPolygon(std::span<const Vec2> points,
                            std::stop_token stop = {});
[[nodiscard]] bool terrainPolygonContains(std::span<const Vec2> points,
                                          Vec2 position);
} // namespace demi::runtime
