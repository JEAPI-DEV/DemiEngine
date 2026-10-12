#include "demi/runtime/render/bgfx3d/TerrainPatchTopology3D.h"

#include "demi/runtime/terrain/TerrainHeightField.h"
#include "demi/runtime/terrain/TerrainLodGeometry.h"

#include <limits>
#include <stdexcept>

namespace demi::runtime::render {
namespace {

std::uint64_t vertexCountFor(const TerrainPatchTopologyKey3D &key, int stride) {
  const auto columns = std::uint64_t(key.cellsX / stride) +
                       std::uint64_t(key.cellsX % stride != 0) + 1;
  const auto rows = std::uint64_t(key.cellsZ / stride) +
                    std::uint64_t(key.cellsZ % stride != 0) + 1;
  // The coarse lattice already includes each coarse perimeter point. The
  // remaining fine perimeter samples are added exactly once, excluding corners.
  const auto addedBoundary = 2 * (std::uint64_t(key.cellsX) - (columns - 1)) +
                             2 * (std::uint64_t(key.cellsZ) - (rows - 1));
  return columns * rows + addedBoundary;
}

} // namespace

PrimitiveMeshData3D
createTerrainPatchTopology3D(const TerrainPatchTopologyKey3D &key) {
  const int stride = terrainLodStep(key.level);
  if (key.cellsX <= 0 || key.cellsZ <= 0 || stride <= 0) {
    throw std::invalid_argument("Terrain patch topology requires positive "
                                "dimensions and a surface level");
  }
  const auto vertices = vertexCountFor(key, stride);
  const auto boundary =
      2 * (std::uint64_t(key.cellsX) + std::uint64_t(key.cellsZ));
  // A triangulated disk has T = 2V - B - 2. Keep this preflight in 64 bits
  // even on 32-bit hosts, before the shared builder allocates size_t arrays.
  const auto triangles = 2 * vertices - boundary - 2;
  const auto maximumCount = std::numeric_limits<std::uint32_t>::max();
  if (vertices > maximumCount || triangles > maximumCount / 3) {
    throw std::length_error(
        "Terrain patch exceeds 32-bit GPU geometry addressing");
  }

  const TerrainChunk rectangle{0, 0, key.cellsX, key.cellsZ};
  const auto topology =
      terrain_detail::buildTerrainLodTopology(rectangle, key.level);
  PrimitiveMeshData3D result;
  result.positions.reserve(topology.samples.size());
  result.textureCoordinates.reserve(topology.samples.size());
  result.indices.reserve(topology.indices.size());
  for (const auto sample : topology.samples) {
    result.positions.push_back(
        {float(sample.cellX), 0.0F, float(sample.cellZ)});
    result.textureCoordinates.push_back(
        {float(double(sample.cellX) / key.cellsX),
         float(double(sample.cellZ) / key.cellsZ)});
  }
  for (const auto index : topology.indices) {
    result.indices.push_back(static_cast<std::uint32_t>(index));
  }
  return result;
}

} // namespace demi::runtime::render
