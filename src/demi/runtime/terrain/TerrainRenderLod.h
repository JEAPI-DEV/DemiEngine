#pragma once

#include "demi/runtime/scene/model/MeshMaterialSections3D.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <array>
#include <cstddef>
#include <map>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace demi::runtime {

struct HeightField;
struct TerrainChunk;
struct MeshRendererComponent;

struct TerrainLodSurface {
  std::shared_ptr<const MeshRendererComponent> geometry;
  MeshMaterialSections3D sections;
  // Expanded triangle sample order used to rebuild material-role sections
  // without replacing geometry when an explicit biome override changes.
  std::shared_ptr<const std::vector<std::size_t>> sampleIndices;
};

struct PreparedTerrainChunkLod {
  // Whole-chunk terrain-local bounds shared by every biome group and level.
  Vec3 boundsMin;
  Vec3 boundsMax;
  std::array<float, 4> geometricErrors{};
  // Whole-chunk counts let every biome choose the same reducing variant.
  std::array<std::size_t, 4> triangleCounts{};
  // Level zero uses the existing Full publication; reduced groups are
  // immutable.
  std::array<std::map<std::string, TerrainLodSurface, std::less<>>, 4> levels;
};

// Derived native render selection data. Collision remains on the original Full
// terrain surface and is never changed by selecting these render meshes.
struct TerrainRenderLod {
  std::shared_ptr<const PreparedTerrainChunkLod> chunk;
  std::string biome;
  std::string owner;
};

[[nodiscard]] std::shared_ptr<const PreparedTerrainChunkLod>
prepareTerrainRenderLod(const HeightField &field, const TerrainChunk &chunk,
                        std::stop_token stop = {});

// Bindings/biome overrides can change independently of immutable geometry.
// UV-scale or surface edits require preparation instead of this rebind path.
[[nodiscard]] std::shared_ptr<const PreparedTerrainChunkLod>
rebindTerrainRenderLod(const HeightField &field,
                       const PreparedTerrainChunkLod &previous);

} // namespace demi::runtime
