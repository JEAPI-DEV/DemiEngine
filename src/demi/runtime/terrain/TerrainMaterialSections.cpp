#include "demi/runtime/terrain/TerrainMaterialSections.h"

#include "demi/runtime/terrain/TerrainHeightField.h"
#include "demi/runtime/terrain/TerrainSurfaceMaterials.h"

#include <atomic>
#include <limits>
#include <map>
#include <stdexcept>

namespace demi::runtime::terrain_detail {
namespace {

using RoleIndexGroups =
    std::map<std::string, std::vector<std::uint32_t>, std::less<>>;

RoleIndexGroups groupTrianglesByRole(
    const TerrainSurfaceClassification &classification,
    std::span<const std::size_t> samples) {
  RoleIndexGroups groups;
  for (std::size_t firstVertex = 0; firstVertex < samples.size(); firstVertex += 3) {
    const auto firstRole = classification.samples.at(samples[firstVertex]);
    const auto secondRole = classification.samples.at(samples[firstVertex + 1]);
    const auto thirdRole = classification.samples.at(samples[firstVertex + 2]);

    // A majority wins; three distinct roles use the first vertex deterministically.
    const auto triangleRole = secondRole == thirdRole ? secondRole : firstRole;
    const auto &roleName = classification.roles.at(triangleRole);
    auto &indices = groups[roleName];
    for (std::size_t vertexOffset = 0; vertexOffset < 3; ++vertexOffset) {
      indices.push_back(static_cast<std::uint32_t>(firstVertex + vertexOffset));
    }
  }
  return groups;
}

} // namespace

MeshMaterialSections3D rebindTerrainMaterialSections(
    const HeightField &field, std::size_t biome,
    const MeshMaterialSections3D &previous) {
  auto result = previous;
  const auto &biomeMaterial = field.biomeMaterial(biome);
  for (auto &section : result.sections) {
    section.material = biomeMaterial;
    if (!biomeMaterial.empty() || !field.surfaceMaterials) {
      continue;
    }

    const auto &bindings = field.surfaceMaterials->bindings;
    const auto binding = bindings.find(section.id);
    if (binding != bindings.end()) {
      section.material = binding->second;
    }
  }
  return result;
}

MeshMaterialSections3D buildTerrainMaterialSections(
    const HeightField &field, std::size_t biome,
    std::span<const std::size_t> samples,
    const MeshMaterialSections3D *previous) {
  if (!field.surfaceMaterials || !field.biomeMaterial(biome).empty()) {
    return {};
  }
  if (samples.size() % 3 != 0) {
    throw std::invalid_argument("Terrain sections require complete triangles");
  }
  if (samples.size() > std::numeric_limits<std::uint32_t>::max()) {
    throw std::invalid_argument("Terrain section exceeds GPU index capacity");
  }

  const auto &classification = *field.surfaceMaterials->classification;
  const auto groups = groupTrianglesByRole(classification, samples);
  auto indices = std::make_shared<std::vector<std::uint32_t>>();
  indices->reserve(samples.size());

  MeshMaterialSections3D result;
  for (const auto &[role, groupIndices] : groups) {
    MeshMaterialSection3D section;
    section.id = role;
    section.firstIndex = static_cast<std::uint32_t>(indices->size());
    section.indexCount = static_cast<std::uint32_t>(groupIndices.size());
    result.sections.push_back(std::move(section));
    indices->insert(indices->end(), groupIndices.begin(), groupIndices.end());
  }

  // Binding-only edits must retain GPU geometry and its revision.
  const bool indicesUnchanged =
      previous && previous->indices && *previous->indices == *indices;
  if (indicesUnchanged) {
    result.indices = previous->indices;
    result.indexRevision = previous->indexRevision;
  } else {
    static std::atomic<std::uint64_t> nextRevision{1};
    result.indices = std::move(indices);
    result.indexRevision = nextRevision.fetch_add(1, std::memory_order_relaxed);
  }
  return rebindTerrainMaterialSections(field, biome, result);
}
} // namespace demi::runtime::terrain_detail
