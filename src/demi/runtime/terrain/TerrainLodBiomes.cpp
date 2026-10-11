#include "demi/runtime/terrain/TerrainLodBiomes.h"

#include "demi/runtime/terrain/TerrainGeometryUv.h"
#include "demi/runtime/terrain/TerrainHeightField.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <stdexcept>
#include <tuple>

namespace demi::runtime {
namespace {

constexpr std::size_t Unmapped = std::numeric_limits<std::size_t>::max();

bool chunkFits(const HeightField &field, const TerrainChunk &chunk) {
  return chunk.cellsX > 0 && chunk.cellsZ > 0 && chunk.firstCellX >= 0 &&
         chunk.firstCellZ >= 0 && chunk.firstCellX <= field.cellsX &&
         chunk.firstCellZ <= field.cellsZ &&
         chunk.cellsX <= field.cellsX - chunk.firstCellX &&
         chunk.cellsZ <= field.cellsZ - chunk.firstCellZ;
}

std::size_t triangleBiome(const HeightField &field,
                          const std::array<std::size_t, 3> &samples) {
  const auto first = field.biomeIndices.at(samples[0]);
  const auto second = field.biomeIndices.at(samples[1]);
  const auto third = field.biomeIndices.at(samples[2]);
  return second == third ? second : first;
}

// Vote the biome identities of actual Full triangles, rather than raw samples.
// An isolated sample may name a biome without owning any Full triangle; it must
// not acquire a visible surface merely because the camera selects coarse LOD.
std::optional<std::size_t> regionBiome(const HeightField &field,
                                       const TerrainChunk &region) {
  std::vector<std::size_t> counts(field.biomeIds.size(), 0);
  for (int z = region.firstCellZ; z < region.firstCellZ + region.cellsZ; ++z) {
    for (int x = region.firstCellX; x < region.firstCellX + region.cellsX;
         ++x) {
      const auto a = field.index(x, z);
      const auto b = field.index(x + 1, z);
      const auto c = field.index(x, z + 1);
      const auto d = field.index(x + 1, z + 1);
      for (const auto label :
           {triangleBiome(field, {a, c, b}), triangleBiome(field, {b, c, d})}) {
        if (label < counts.size()) {
          ++counts[label];
        }
      }
    }
  }
  std::optional<std::size_t> winner;
  for (std::size_t label = 0; label < counts.size(); ++label) {
    if (counts[label] != 0 && (!winner || counts[label] > counts[*winner])) {
      winner = label;
    }
  }
  return winner;
}

void validateField(const HeightField &field) {
  if (field.biomeIds.empty()) {
    throw std::invalid_argument("Terrain field names no biome");
  }
  if (field.cellsX <= 0 || field.cellsZ <= 0 || !std::isfinite(field.size.x) ||
      !std::isfinite(field.size.y) || field.size.x <= 0 || field.size.y <= 0) {
    throw std::invalid_argument("Terrain field has no valid grid extent");
  }
  const auto samples = field.index(field.cellsX, field.cellsZ) + 1;
  if (field.heights.size() < samples || field.biomeIndices.size() < samples) {
    throw std::invalid_argument("Terrain field buffers do not cover its grid");
  }
}

} // namespace

std::size_t terrainLodBiomeForCell(const HeightField &field, int cellX,
                                   int cellZ, int step) {
  validateField(field);
  if (step <= 0 || cellX < 0 || cellZ < 0 || cellX >= field.cellsX ||
      cellZ >= field.cellsZ) {
    throw std::invalid_argument(
        "Terrain biome selection requires a valid cell and stride");
  }
  const int firstX = (cellX / step) * step;
  const int firstZ = (cellZ / step) * step;
  const TerrainChunk region{firstX, firstZ,
                            std::min(step, field.cellsX - firstX),
                            std::min(step, field.cellsZ - firstZ)};
  const auto label = regionBiome(field, region);
  if (!label) {
    throw std::invalid_argument(
        "Terrain cell has no named Full-triangle biome");
  }
  return *label;
}

std::optional<TerrainLodBiomeMesh>
buildTerrainLodBiomeMesh(const HeightField &field, const TerrainChunk &chunk,
                         TerrainLod level, float skirtDepth) {
  const auto ordinal = terrainLodIndex(level);
  if (ordinal < 0 || ordinal >= terrainLodLevelCount) {
    throw std::invalid_argument(
        "Terrain biome mesh requires a valid LOD level");
  }
  if (level == TerrainLod::Off) {
    return std::nullopt;
  }
  validateField(field);
  if (!chunkFits(field, chunk)) {
    throw std::invalid_argument(
        "Terrain biome mesh chunk does not fit its field");
  }
  const auto flat = buildTerrainLodMesh(field, chunk, level, skirtDepth);
  if (!flat) {
    return std::nullopt;
  }
  if (flat->sampleIndices.size() != flat->vertices.size() ||
      flat->triangles.size() * 3 != flat->indices.size()) {
    throw std::logic_error(
        "Terrain LOD mesh is missing triangle/sample provenance");
  }

  TerrainLodBiomeMesh result;
  result.sourceCells = flat->sourceCells;
  std::vector<std::optional<std::size_t>> labels(flat->triangles.size());
  std::vector<bool> used(field.biomeIds.size(), false);
  using RegionKey = std::tuple<int, int, int, int>;
  std::map<RegionKey, std::optional<std::size_t>> regionLabels;
  for (std::size_t triangle = 0; triangle < flat->triangles.size();
       ++triangle) {
    const auto &source = flat->triangles[triangle];
    std::optional<std::size_t> label;
    if (level == TerrainLod::Full && !source.skirt) {
      const auto first = triangle * 3;
      const auto owner = triangleBiome(
          field, {flat->sampleIndices.at(flat->indices.at(first)),
                  flat->sampleIndices.at(flat->indices.at(first + 1)),
                  flat->sampleIndices.at(flat->indices.at(first + 2))});
      if (owner < field.biomeIds.size()) {
        label = owner;
      }
    } else {
      const RegionKey key{source.firstCellX, source.firstCellZ, source.cellsX,
                          source.cellsZ};
      auto [entry, inserted] = regionLabels.try_emplace(key);
      if (inserted) {
        entry->second =
            regionBiome(field, {source.firstCellX, source.firstCellZ,
                                source.cellsX, source.cellsZ});
      }
      label = entry->second;
    }
    labels[triangle] = label;
    if (label) {
      used[*label] = true;
    } else {
      result.skippedBiomes += source.sourceCells;
    }
  }

  std::vector<std::size_t> groupOf(field.biomeIds.size(), Unmapped);
  for (std::size_t label = 0; label < used.size(); ++label) {
    if (!used[label]) {
      continue;
    }
    groupOf[label] = result.groups.size();
    TerrainLodBiomeGroup group;
    group.biomeId = field.biomeIds[label];
    if (label < field.biomeColors.size()) {
      group.tint = field.biomeColors[label];
    }
    result.groups.push_back(std::move(group));
  }
  std::vector<std::vector<std::size_t>> localIndices(
      result.groups.size(),
      std::vector<std::size_t>(flat->vertices.size(), Unmapped));
  std::vector<bool> skirtOwned(flat->vertices.size(), false);
  for (std::size_t triangle = 0; triangle < labels.size(); ++triangle) {
    if (!labels[triangle]) {
      continue;
    }
    const auto label = *labels[triangle];
    const auto groupIndex = groupOf[label];
    auto &group = result.groups[groupIndex];
    const auto &source = flat->triangles[triangle];
    group.sourceCells += source.sourceCells;
    for (std::size_t corner = 0; corner < 3; ++corner) {
      const auto flatIndex = flat->indices.at(triangle * 3 + corner);
      auto &local = localIndices[groupIndex].at(flatIndex);
      const auto sample = flat->sampleIndices.at(flatIndex);
      const auto vertex = flat->vertices.at(flatIndex);
      if (local == Unmapped) {
        local = group.vertices.size();
        group.vertices.push_back(vertex);
        group.normals.push_back(flat->normals.at(flatIndex));
        group.uvs.push_back(terrain_detail::terrainUv(
            {vertex.x, vertex.z}, field.biomeTextureScale(label)));
        group.sampleIndices.push_back(sample);
      }
      group.indices.push_back(local);
      const bool isSkirtBottom =
          source.skirt && vertex.y < field.heights.at(sample);
      if (isSkirtBottom && !skirtOwned[flatIndex]) {
        skirtOwned[flatIndex] = true;
        ++group.skirtDepths;
      }
    }
  }
  return result;
}

} // namespace demi::runtime
