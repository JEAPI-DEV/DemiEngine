#include "demi/runtime/terrain/TerrainRenderLod.h"

#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/terrain/TerrainHeightField.h"
#include "demi/runtime/terrain/TerrainLod.h"
#include "demi/runtime/terrain/TerrainLodBiomes.h"
#include "demi/runtime/terrain/TerrainMaterialSections.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace demi::runtime {
namespace {

std::size_t biomeIndex(const HeightField &field, const std::string &id) {
  const auto found =
      std::find(field.biomeIds.begin(), field.biomeIds.end(), id);
  if (found == field.biomeIds.end()) {
    throw std::invalid_argument(
        "Prepared terrain LOD names an unknown biome: " + id);
  }
  return std::size_t(found - field.biomeIds.begin());
}

bool prepareBounds(const HeightField &field, const TerrainChunk &chunk,
                   PreparedTerrainChunkLod &result, std::stop_token stop) {
  const bool validChunk = chunk.cellsX > 0 && chunk.cellsZ > 0 &&
                          chunk.firstCellX >= 0 && chunk.firstCellZ >= 0 &&
                          chunk.firstCellX <= field.cellsX &&
                          chunk.cellsX <= field.cellsX - chunk.firstCellX &&
                          chunk.firstCellZ <= field.cellsZ &&
                          chunk.cellsZ <= field.cellsZ - chunk.firstCellZ;
  if (!validChunk || !std::isfinite(field.size.x) ||
      !std::isfinite(field.size.y) || field.size.x <= 0.0F ||
      field.size.y <= 0.0F) {
    throw std::invalid_argument(
        "Prepared terrain LOD requires a valid chunk and extent");
  }
  const auto first = field.position(chunk.firstCellX, chunk.firstCellZ);
  const auto last = field.position(chunk.firstCellX + chunk.cellsX,
                                   chunk.firstCellZ + chunk.cellsZ);
  const float firstHeight = field.height(chunk.firstCellX, chunk.firstCellZ);
  result.boundsMin = {first.x, firstHeight, first.y};
  result.boundsMax = {last.x, firstHeight, last.y};
  for (int z = chunk.firstCellZ; z <= chunk.firstCellZ + chunk.cellsZ; ++z) {
    if (stop.stop_requested()) {
      return false;
    }
    for (int x = chunk.firstCellX; x <= chunk.firstCellX + chunk.cellsX; ++x) {
      const float height = field.height(x, z);
      if (!std::isfinite(height)) {
        throw std::invalid_argument(
            "Prepared terrain LOD contains a non-finite height");
      }
      result.boundsMin.y = std::min(result.boundsMin.y, height);
      result.boundsMax.y = std::max(result.boundsMax.y, height);
    }
  }
  return true;
}

TerrainLodSurface prepareSurface(const HeightField &field,
                                 const TerrainLodBiomeGroup &group,
                                 std::stop_token stop) {
  auto geometry = std::make_shared<MeshRendererComponent>();
  auto samples = std::make_shared<std::vector<std::size_t>>();
  geometry->vertices.reserve(group.indices.size());
  geometry->normals.reserve(group.indices.size());
  geometry->uvs.reserve(group.indices.size());
  samples->reserve(group.indices.size());
  for (const auto index : group.indices) {
    if (stop.stop_requested()) {
      return {};
    }
    geometry->vertices.push_back(group.vertices.at(index));
    geometry->normals.push_back(group.normals.at(index));
    geometry->uvs.push_back(group.uvs.at(index));
    samples->push_back(group.sampleIndices.at(index));
  }
  geometry->markGeometryChanged();
  auto sections = terrain_detail::buildTerrainMaterialSections(
      field, biomeIndex(field, group.biomeId), *samples);
  return {.geometry = std::move(geometry),
          .sections = std::move(sections),
          .sampleIndices = std::move(samples)};
}

} // namespace

std::shared_ptr<const PreparedTerrainChunkLod>
prepareTerrainRenderLod(const HeightField &field, const TerrainChunk &chunk,
                        std::stop_token stop) {
  if (stop.stop_requested()) {
    return {};
  }
  auto result = std::make_shared<PreparedTerrainChunkLod>();
  if (!prepareBounds(field, chunk, *result, stop)) {
    return {};
  }
  result->triangleCounts[0] =
      2 * std::size_t(chunk.cellsX) * std::size_t(chunk.cellsZ);
  for (std::size_t level = 1; level < result->levels.size(); ++level) {
    if (stop.stop_requested()) {
      return {};
    }
    const auto detail = static_cast<TerrainLod>(level);
    result->geometricErrors[level] =
        terrainLodGeometricError(field, chunk, detail);
    if (stop.stop_requested()) {
      return {};
    }
    const auto mesh = buildTerrainLodBiomeMesh(field, chunk, detail, 0.0F);
    if (!mesh || mesh->skippedBiomes != 0) {
      throw std::invalid_argument(
          "Prepared terrain LOD cannot omit unnamed biome surfaces");
    }
    for (const auto &group : mesh->groups) {
      auto surface = prepareSurface(field, group, stop);
      if (!surface.geometry || stop.stop_requested()) {
        return {};
      }
      result->triangleCounts[level] += surface.geometry->vertices.size() / 3;
      result->levels[level].emplace(group.biomeId, std::move(surface));
    }
  }
  if (stop.stop_requested()) {
    return {};
  }
  return result;
}

std::shared_ptr<const PreparedTerrainChunkLod>
rebindTerrainRenderLod(const HeightField &field,
                       const PreparedTerrainChunkLod &previous) {
  auto result = std::make_shared<PreparedTerrainChunkLod>(previous);
  for (auto &level : result->levels) {
    for (auto &[biome, surface] : level) {
      if (!surface.geometry || !surface.sampleIndices) {
        throw std::invalid_argument(
            "Prepared terrain LOD surface has no geometry provenance");
      }
      surface.sections = terrain_detail::buildTerrainMaterialSections(
          field, biomeIndex(field, biome), *surface.sampleIndices,
          &surface.sections);
    }
  }
  return result;
}

} // namespace demi::runtime
