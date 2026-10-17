#pragma once
#include "demi/runtime/scene/model/ComponentStorage.h"
#include "demi/runtime/terrain/TerrainPalette.h"
#include <nlohmann/json_fwd.hpp>

namespace demi::runtime {
struct Entity;
struct TerrainScatterCollisionState {
  TerrainCollisionPolicy policy = TerrainCollisionPolicy::Static;
  ComponentStorage authored;
};

// The authored prefab override equivalent of the native policy below. Expands
// presets for discovery while returning only physics component overrides.
[[nodiscard]] nlohmann::json
terrainScatterCollisionOverrides(const nlohmann::json &entity,
                                 TerrainCollisionPolicy policy);

// Scatter policies control physics participation without replacing authored
// sphere/capsule/convex/model geometry with an invented proxy shape.
void applyTerrainScatterCollision(Entity &entity,
                                  TerrainCollisionPolicy policy);
} // namespace demi::runtime
