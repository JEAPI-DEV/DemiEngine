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
prepareTerrainRenderLod(std::shared_ptr<const HeightField> field,
                        const TerrainChunk &chunk, std::stop_token stop) {
  if (stop.stop_requested()) {
    return {};
  }
  if (!field) {
    throw std::invalid_argument(
        "Prepared terrain LOD requires a field snapshot");
  }
  auto result = std::make_shared<PreparedTerrainChunkLod>();
  if (!prepareBounds(*field, chunk, *result, stop)) {
    return {};
  }
  result->field = std::move(field);
  result->rectangle = chunk;
  result->geometryIdentity = std::make_shared<TerrainLodGeometryIdentity>();
  result->triangleCounts[0] =
      2 * std::size_t(chunk.cellsX) * std::size_t(chunk.cellsZ);
  for (std::size_t level = 1; level < result->geometricErrors.size(); ++level) {
    if (stop.stop_requested()) {
      return {};
    }
    const auto detail = static_cast<TerrainLod>(level);
    result->geometricErrors[level] =
        terrainLodGeometricError(*result->field, chunk, detail);
    result->triangleCounts[level] =
        terrainLodSurfaceTriangleCount(chunk.cellsX, chunk.cellsZ, detail);
  }
  if (stop.stop_requested()) {
    return {};
  }
  return result;
}

std::shared_ptr<const PreparedTerrainLodLevel>
prepareTerrainRenderLodLevel(const PreparedTerrainChunkLod &chunk,
                             std::size_t level, std::stop_token stop) {
  if (stop.stop_requested()) {
    return {};
  }
  if (!chunk.field || !chunk.geometryIdentity || level == 0 ||
      level >= chunk.geometricErrors.size()) {
    throw std::invalid_argument(
        "Terrain render preparation requires a reduced level and descriptor");
  }
  const auto mesh = buildTerrainLodBiomeMesh(
      *chunk.field, chunk.rectangle, static_cast<TerrainLod>(level), 0.0F);
  if (!mesh || mesh->skippedBiomes != 0) {
    throw std::invalid_argument(
        "Prepared terrain LOD cannot omit unnamed biome surfaces");
  }
  auto result = std::make_shared<PreparedTerrainLodLevel>();
  std::size_t triangles = 0;
  for (const auto &group : mesh->groups) {
    auto surface = prepareSurface(*chunk.field, group, stop);
    if (!surface.geometry || stop.stop_requested()) {
      return {};
    }
    triangles += surface.geometry->vertices.size() / 3;
    const auto inserted =
        result->emplace(group.biomeId, std::move(surface)).second;
    if (!inserted) {
      throw std::invalid_argument(
          "Prepared terrain LOD repeats a biome identity");
    }
  }
  if (triangles != chunk.triangleCounts[level]) {
    throw std::invalid_argument(
        "Prepared terrain LOD omitted surface triangles");
  }
  if (stop.stop_requested()) {
    return {};
  }
  return result;
}

std::shared_ptr<const PreparedTerrainChunkLod>
rebindTerrainRenderLod(std::shared_ptr<const HeightField> field,
                       const PreparedTerrainChunkLod &previous) {
  if (!field || !previous.field || !previous.geometryIdentity) {
    throw std::invalid_argument(
        "Terrain LOD rebinding requires immutable field snapshots");
  }
  const auto &before = *previous.field;
  const bool incompatibleGeometry =
      field->cellsX != before.cellsX || field->cellsZ != before.cellsZ ||
      field->size.x != before.size.x || field->size.y != before.size.y ||
      field->biomeIds != before.biomeIds ||
      field->biomeTextureScales != before.biomeTextureScales;
  if (incompatibleGeometry) {
    throw std::invalid_argument(
        "Terrain geometry and UV changes require new LOD preparation");
  }
  auto result = std::make_shared<PreparedTerrainChunkLod>(previous);
  result->field = std::move(field);
  return result;
}

std::shared_ptr<const PreparedTerrainLodLevel>
rebindTerrainRenderLodLevel(const PreparedTerrainChunkLod &chunk,
                            const PreparedTerrainLodLevel &previous) {
  if (!chunk.field || !chunk.geometryIdentity) {
    throw std::invalid_argument(
        "Terrain LOD section rebinding requires a descriptor");
  }
  auto result = std::make_shared<PreparedTerrainLodLevel>(previous);
  for (auto &[biome, surface] : *result) {
    if (!surface.geometry || !surface.sampleIndices) {
      throw std::invalid_argument(
          "Prepared terrain LOD surface has no geometry provenance");
    }
    surface.sections = terrain_detail::buildTerrainMaterialSections(
        *chunk.field, biomeIndex(*chunk.field, biome), *surface.sampleIndices,
        &surface.sections);
  }
  return result;
}

} // namespace demi::runtime
