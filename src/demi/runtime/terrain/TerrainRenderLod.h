#pragma once

#include "demi/runtime/scene/model/MeshMaterialSections3D.h"
#include "demi/runtime/scene/model/SceneTypes.h"
#include "demi/runtime/terrain/TerrainHeightField.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace demi::runtime {

struct HeightField;
struct MeshRendererComponent;

struct TerrainLodSurface {
  std::shared_ptr<const MeshRendererComponent> geometry;
  MeshMaterialSections3D sections;
  // Expanded triangle sample order used to rebuild material-role sections
  // without replacing geometry when an explicit biome override changes.
  std::shared_ptr<const std::vector<std::size_t>> sampleIndices;
};

using PreparedTerrainLodLevel =
    std::map<std::string, TerrainLodSurface, std::less<>>;

// Shared tag identifies geometry independently of appearance snapshots.
struct TerrainLodGeometryIdentity {};

struct PreparedTerrainChunkLod {
  // Whole-chunk terrain-local bounds shared by every biome group and level.
  Vec3 boundsMin;
  Vec3 boundsMax;
  std::array<float, 4> geometricErrors{};
  // Whole-chunk counts let every biome choose the same reducing variant.
  std::array<std::size_t, 4> triangleCounts{};
  std::shared_ptr<const HeightField> field;
  TerrainChunk rectangle;
  std::shared_ptr<const TerrainLodGeometryIdentity> geometryIdentity;
};

// Derived native render selection data. Collision remains on the original Full
// terrain surface and is never changed by selecting these render meshes.
struct TerrainRenderLod {
  std::shared_ptr<const PreparedTerrainChunkLod> chunk;
  std::string biome;
  std::string owner;
  // Native publication stamp; live geometry edits use the ordinary mesh path.
  std::uint64_t fullGeometryRevision = 0;
};

// Prepares selection metadata and retains the existing immutable source. Does
// not allocate reduced render buffers; level zero uses the existing Full mesh.
[[nodiscard]] std::shared_ptr<const PreparedTerrainChunkLod>
prepareTerrainRenderLod(std::shared_ptr<const HeightField> field,
                        const TerrainChunk &chunk, std::stop_token stop = {});

// Builds only the requested reduced level (1..3). Cancellation publishes no
// partial level. Call from bounded preparation work, not frame submission.
[[nodiscard]] std::shared_ptr<const PreparedTerrainLodLevel>
prepareTerrainRenderLodLevel(const PreparedTerrainChunkLod &chunk,
                             std::size_t level, std::stop_token stop = {});

// Bindings/biome overrides can change independently of immutable geometry.
// UV-scale or surface edits require new preparation and geometry identity.
[[nodiscard]] std::shared_ptr<const PreparedTerrainChunkLod>
rebindTerrainRenderLod(std::shared_ptr<const HeightField> field,
                       const PreparedTerrainChunkLod &previous);

[[nodiscard]] std::shared_ptr<const PreparedTerrainLodLevel>
rebindTerrainRenderLodLevel(const PreparedTerrainChunkLod &chunk,
                            const PreparedTerrainLodLevel &previous);

} // namespace demi::runtime
