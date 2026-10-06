#include "demi/runtime/terrain/TerrainUpdate.h"
#include <algorithm>
#include <map>
#include <stdexcept>

namespace demi::runtime {
bool TerrainRect::contains(int x, int z) const {
  return !empty() && x >= minX && x <= maxX && z >= minZ && z <= maxZ;
}
bool TerrainRect::intersects(const TerrainRect &other) const {
  return !empty() && !other.empty() && minX <= other.maxX &&
         maxX >= other.minX && minZ <= other.maxZ && maxZ >= other.minZ;
}
void TerrainRect::include(int x, int z) { include({x, z, x, z}); }
void TerrainRect::include(const TerrainRect &other) {
  if (other.empty())
    return;
  if (empty()) {
    *this = other;
    return;
  }
  minX = std::min(minX, other.minX);
  minZ = std::min(minZ, other.minZ);
  maxX = std::max(maxX, other.maxX);
  maxZ = std::max(maxZ, other.maxZ);
}
TerrainRect TerrainRect::expanded(int samples, int cellsX, int cellsZ) const {
  if (empty())
    return {};
  return {int(std::max<std::int64_t>(0, std::int64_t(minX) - samples)),
          int(std::max<std::int64_t>(0, std::int64_t(minZ) - samples)),
          int(std::min<std::int64_t>(cellsX, std::int64_t(maxX) + samples)),
          int(std::min<std::int64_t>(cellsZ, std::int64_t(maxZ) + samples))};
}
TerrainRect TerrainInvalidation::geometrySamples() const {
  auto result = heightSamples;
  result.include(normalSamples);
  result.include(biomeSamples);
  return result;
}

namespace {
bool same(const TerrainSampleValue &a, const TerrainSampleValue &b) {
  return a.base == b.base && a.height == b.height &&
         a.exclusion == b.exclusion && a.biome == b.biome &&
         a.normal.x == b.normal.x && a.normal.y == b.normal.y &&
         a.normal.z == b.normal.z;
}
TerrainSampleValue sample(const HeightField &field, std::size_t index) {
  return {field.baseHeights.at(index), field.heights.at(index),
          field.exclusions.at(index), field.normals.at(index),
          field.biomeIndices.at(index)};
}
void write(HeightField &field, std::size_t index,
           const TerrainSampleValue &value) {
  // Do not detach pages in channels whose value did not change.
  const HeightField &read = field;
  if (read.baseHeights.at(index) != value.base)
    field.baseHeights.set(index, value.base);
  if (read.heights.at(index) != value.height)
    field.heights.set(index, value.height);
  if (read.exclusions.at(index) != value.exclusion)
    field.exclusions.set(index, value.exclusion);
  if (read.biomeIndices.at(index) != value.biome)
    field.biomeIndices.set(index, value.biome);
  const auto normal = read.normals.at(index);
  if (normal.x != value.normal.x || normal.y != value.normal.y ||
      normal.z != value.normal.z)
    field.normals.set(index, value.normal);
}
TerrainInvalidation mergedInvalidation(TerrainInvalidation first,
                                       const TerrainInvalidation &next) {
  first.fullGeneration |= next.fullGeneration;
  first.layoutChanged |= next.layoutChanged;
  first.materialsChanged |= next.materialsChanged;
  first.baseSamples.include(next.baseSamples);
  first.heightSamples.include(next.heightSamples);
  first.normalSamples.include(next.normalSamples);
  first.biomeSamples.include(next.biomeSamples);
  first.exclusionSamples.include(next.exclusionSamples);
  if (!next.reason.empty())
    first.reason = next.reason;
  return first;
}
} // namespace

namespace {
bool sameBiomePalette(const TerrainBiomePalette &a,
                         const TerrainBiomePalette &b) {
  if (a.ids != b.ids || a.colors.size() != b.colors.size() ||
      a.materials != b.materials || a.textureScales != b.textureScales)
    return false;
  for (std::size_t i = 0; i < a.colors.size(); ++i) {
    const auto left = a.colors[i], right = b.colors[i];
    if (left.r != right.r || left.g != right.g || left.b != right.b ||
        left.a != right.a)
      return false;
  }
  return true;
}
bool samePlacement(const TerrainScatterPlacement &a,
                   const TerrainScatterPlacement &b) {
  return a.role == b.role && a.asset == b.asset && a.prefab == b.prefab &&
         a.biome == b.biome && a.biomeName == b.biomeName &&
         a.cell == b.cell && a.position.x == b.position.x &&
         a.position.y == b.position.y && a.position.z == b.position.z &&
         a.yaw == b.yaw && a.scale == b.scale &&
         a.collision == b.collision && a.lod == b.lod;
}
bool sameDerived(const TerrainDerivedState &a, const TerrainDerivedState &b) {
  if (a.scatterTruncated != b.scatterTruncated ||
      a.graphArtifacts != b.graphArtifacts ||
      a.placements.size() != b.placements.size())
    return false;
  for (std::size_t index = 0; index < a.placements.size(); ++index)
    if (!samePlacement(a.placements[index], b.placements[index]))
      return false;
  return true;
}
bool sameDerived(const HeightField &field, const TerrainDerivedState &expected) {
  if (field.scatterTruncated != expected.scatterTruncated ||
      field.graphArtifacts != expected.graphArtifacts ||
      field.scatterPlacements.size() != expected.placements.size())
    return false;
  for (std::size_t index = 0; index < field.scatterPlacements.size(); ++index)
    if (!samePlacement(field.scatterPlacements[index],
                       expected.placements[index]))
      return false;
  return true;
}
} // namespace

bool terrainFieldsEqual(const HeightField &left, const HeightField &right) {
  if (&left == &right)
    return true;
  if (left.size.x != right.size.x || left.size.y != right.size.y ||
      left.cellsX != right.cellsX || left.cellsZ != right.cellsZ ||
      left.heights != right.heights || left.baseHeights != right.baseHeights ||
      left.biomeIndices != right.biomeIndices ||
      left.exclusions != right.exclusions ||
      left.normals.size() != right.normals.size() ||
      left.chunks.size() != right.chunks.size() ||
      !sameBiomePalette({left.biomeIds, left.biomeColors,
                         left.biomeMaterials, left.biomeTextureScales},
                        {right.biomeIds, right.biomeColors,
                         right.biomeMaterials, right.biomeTextureScales}))
    return false;
  for (std::size_t i = 0; i < left.normals.size(); ++i) {
    const auto a = left.normals[i], b = right.normals[i];
    if (a.x != b.x || a.y != b.y || a.z != b.z)
      return false;
  }
  for (std::size_t i = 0; i < left.chunks.size(); ++i) {
    const auto a = left.chunks[i], b = right.chunks[i];
    if (a.firstCellX != b.firstCellX || a.firstCellZ != b.firstCellZ ||
        a.cellsX != b.cellsX || a.cellsZ != b.cellsZ)
      return false;
  }
  return true;
}

std::size_t TerrainPatch::retainedBytes() const {
  std::size_t bytes =
      sizeof(*this) + samples.size() * sizeof(TerrainSampleChange);
  for (const auto *palette : {&beforePalette, &afterPalette}) {
    if (!*palette)
      continue;
    bytes += (*palette)->colors.size() * sizeof(Color);
    bytes += (*palette)->textureScales.size() * sizeof(float);
    for (const auto &id : (*palette)->ids)
      bytes += sizeof(std::string) + id.size();
    for (const auto &material : (*palette)->materials)
      bytes += sizeof(std::string) + material.size();
  }
  for (const auto *derived : {&beforeDerived, &afterDerived})
    if (*derived)
      bytes += (*derived)->placements.size() *
               sizeof(TerrainScatterPlacement);
  // Full regeneration snapshots share pages where possible. This is an upper
  // bound for them; local history has no whole-field snapshots.
  for (const auto &field : {fullBefore, fullAfter}) {
    if (field)
      bytes += field->heights.size() *
               (sizeof(float) * 3 + sizeof(Vec3) + sizeof(std::size_t));
  }
  return bytes;
}

TerrainUpdate applyTerrainPatch(std::shared_ptr<const HeightField> current,
                                const TerrainPatch &patch, bool forward) {
  if (!current)
    throw std::invalid_argument("Terrain history requires a current field");
  auto field = forward ? patch.fullAfter : patch.fullBefore;
  if (patch.fullBefore || patch.fullAfter) {
    const auto expected = forward ? patch.fullBefore : patch.fullAfter;
    if (!field || !expected || !terrainFieldsEqual(*current, *expected))
      throw std::invalid_argument("Terrain global history no longer matches");
  }
  if (!field) {
    if (current->size.x != patch.size.x || current->size.y != patch.size.y ||
        current->cellsX != patch.cellsX || current->cellsZ != patch.cellsZ)
      throw std::invalid_argument("Terrain history grid no longer matches");
    if (patch.beforePalette.has_value() != patch.afterPalette.has_value())
      throw std::invalid_argument(
          "Terrain history requires both palette states");
    if (patch.beforeDerived.has_value() != patch.afterDerived.has_value())
      throw std::invalid_argument(
          "Terrain history requires both derived states");
    const auto &expectedDerived =
        forward ? patch.beforeDerived : patch.afterDerived;
    if (expectedDerived && !sameDerived(*current, *expectedDerived))
      throw std::invalid_argument("Terrain history derived state no longer matches");
    const auto &expectedPalette =
        forward ? patch.beforePalette : patch.afterPalette;
    if (expectedPalette &&
        !sameBiomePalette({current->biomeIds, current->biomeColors,
                           current->biomeMaterials,
                           current->biomeTextureScales},
                          *expectedPalette))
      throw std::invalid_argument("Terrain history palette no longer matches");
    auto updated = std::make_shared<HeightField>(*current);
    for (const auto &change : patch.samples) {
      const auto &expected = forward ? change.before : change.after;
      if (!same(sample(*current, change.index), expected))
        throw std::invalid_argument("Terrain history samples no longer match");
      write(*updated, change.index, forward ? change.after : change.before);
    }
    const auto &palette = forward ? patch.afterPalette : patch.beforePalette;
    if (palette) {
      updated->biomeIds = palette->ids;
      updated->biomeColors = palette->colors;
      updated->biomeMaterials = palette->materials;
      updated->biomeTextureScales = palette->textureScales;
    }
    const auto &derived =
        forward ? patch.afterDerived : patch.beforeDerived;
    if (derived) {
      updated->scatterPlacements = derived->placements;
      updated->scatterTruncated = derived->scatterTruncated;
      updated->graphArtifacts = derived->graphArtifacts;
    }
    field = std::move(updated);
  }
  return {std::move(field),
          std::make_shared<TerrainPatch>(patch),
          patch.invalidation,
          {.changedSamples = patch.samples.size()}};
}

std::shared_ptr<const TerrainPatch>
mergeTerrainPatches(const TerrainPatch &first, const TerrainPatch &next) {
  auto result = std::make_shared<TerrainPatch>(first);
  result->invalidation =
      mergedInvalidation(first.invalidation, next.invalidation);
  if (first.fullBefore || first.fullAfter || next.fullBefore ||
      next.fullAfter) {
    if ((first.fullBefore || first.fullAfter) &&
        (!first.fullBefore || !first.fullAfter))
      throw std::invalid_argument(
          "Cannot merge incomplete global terrain history");
    if ((next.fullBefore || next.fullAfter) &&
        (!next.fullBefore || !next.fullAfter))
      throw std::invalid_argument(
          "Cannot merge incomplete global terrain history");
    if (first.fullAfter && next.fullBefore &&
        !terrainFieldsEqual(*first.fullAfter, *next.fullBefore))
      throw std::invalid_argument(
          "Global terrain patch sequence is not contiguous");
    result->fullBefore = first.fullBefore;
    if (!result->fullBefore)
      result->fullBefore =
          applyTerrainPatch(next.fullBefore, first, false).field;
    result->fullAfter = next.fullAfter;
    if (!result->fullAfter)
      result->fullAfter = applyTerrainPatch(first.fullAfter, next, true).field;
    result->samples.clear();
    result->beforePalette.reset();
    result->afterPalette.reset();
    result->beforeDerived.reset();
    result->afterDerived.reset();
    return result;
  }
  if (first.size.x != next.size.x || first.size.y != next.size.y ||
      first.cellsX != next.cellsX || first.cellsZ != next.cellsZ)
    throw std::invalid_argument(
        "Cannot merge terrain patches from different grids");
  if (first.beforePalette.has_value() != first.afterPalette.has_value() ||
      next.beforePalette.has_value() != next.afterPalette.has_value())
    throw std::invalid_argument("Terrain patch requires both palette states");
  if (first.afterPalette && next.beforePalette &&
      !sameBiomePalette(*first.afterPalette, *next.beforePalette))
    throw std::invalid_argument(
        "Terrain palette patch sequence is not contiguous");
  if (first.beforeDerived.has_value() != first.afterDerived.has_value() ||
      next.beforeDerived.has_value() != next.afterDerived.has_value())
    throw std::invalid_argument("Terrain patch requires both derived states");
  if (first.afterDerived && next.beforeDerived &&
      !sameDerived(*first.afterDerived, *next.beforeDerived))
    throw std::invalid_argument(
        "Terrain derived patch sequence is not contiguous");
  std::map<std::size_t, TerrainSampleChange> samples;
  for (const auto &change : first.samples)
    samples.emplace(change.index, change);
  for (const auto &change : next.samples) {
    auto [entry, inserted] = samples.emplace(change.index, change);
    if (!inserted) {
      if (!same(entry->second.after, change.before))
        throw std::invalid_argument("Terrain patch sequence is not contiguous");
      entry->second.after = change.after;
    }
  }
  result->samples.clear();
  for (const auto &[index, change] : samples)
    if (!same(change.before, change.after))
      result->samples.push_back(change);
  if (!result->beforePalette)
    result->beforePalette = next.beforePalette;
  if (next.afterPalette)
    result->afterPalette = next.afterPalette;
  if (!result->beforeDerived)
    result->beforeDerived = next.beforeDerived;
  if (next.afterDerived)
    result->afterDerived = next.afterDerived;
  return result;
}
} // namespace demi::runtime
