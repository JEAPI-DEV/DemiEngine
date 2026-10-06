#pragma once

#include "demi/runtime/scene/model/Entity.h"

#include <stop_token>
#include <string>
#include <vector>

namespace demi::runtime {
struct HeightField;

namespace terrain_detail {
// Native-only ownership; water surfaces never acquire terrain collision.
struct TerrainGeneratedWaterSurface {
  std::string owner;
};
} // namespace terrain_detail

// Publishes graphArtifacts->waterResult as one native mesh per drawable body.
// Geometry stays terrain-local under the owner's Transform3D. UVs repeat once
// per local unit. Depth produces body-authored per-vertex RGBA; the shared mesh
// renderer interpolates it. Invalid artifacts throw std::invalid_argument.
// Cancellation returns no meshes, including when requested after some bodies
// have been prepared.
[[nodiscard]] std::vector<Entity>
buildTerrainWaterMeshes(const Entity &owner, const HeightField &field,
                        std::stop_token stop = {});

// Compares drawable water surfaces, with shared artifact identity as a fast
// path. Biome data, graph diagnostics and empty bodies do not affect meshes.
// A null before means no previously published water. Does not allocate.
[[nodiscard]] bool terrainWaterMeshesChanged(const HeightField *before,
                                             const HeightField &after);
} // namespace demi::runtime
