#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/terrain/TerrainHeightField.h"
#include "demi/runtime/terrain/TerrainLod.h"
#include "demi/runtime/terrain/TerrainLodBiomes.h"
#include "demi/runtime/terrain/TerrainMaterialSections.h"
#include "demi/runtime/terrain/TerrainRenderLod.h"
#include "demi/runtime/terrain/TerrainSurfaceMaterials.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <iostream>
#include <limits>
#include <stop_token>

using namespace demi::runtime;

namespace {

HeightField fixture() {
  HeightField field;
  field.size = {30, 20};
  field.cellsX = 15;
  field.cellsZ = 10;
  field.biomeIds = {"soil", "stone"};
  field.biomeColors = {{0.5F, 0.4F, 0.3F, 1.0F}, {0.7F, 0.7F, 0.7F, 1.0F}};
  field.biomeTextureScales = {0.25F, 2.0F};
  field.chunks = {{3, 2, 9, 7}};
  const auto count = field.index(field.cellsX, field.cellsZ) + 1;
  field.heights.resize(count);
  field.normals.resize(count);
  field.biomeIndices.resize(count);
  auto classification = std::make_shared<TerrainSurfaceClassification>();
  classification->roles = {"earth", "rock"};
  classification->samples.resize(count);
  for (int z = 0; z <= field.cellsZ; ++z) {
    for (int x = 0; x <= field.cellsX; ++x) {
      const auto sample = field.index(x, z);
      field.heights.set(sample, float((x * 17 + z * 13) % 11));
      field.normals.set(sample, {0, 1, 0});
      field.biomeIndices.set(sample, x < 8 ? 0 : 1);
      classification->samples.set(sample, z < 5 ? 0 : 1);
    }
  }
  auto materials = std::make_shared<TerrainSurfaceMaterials>();
  materials->classification = classification;
  materials->bindings = {{"earth", "asset://materials/earth"},
                         {"rock", "asset://materials/rock"}};
  field.surfaceMaterials = std::move(materials);
  return field;
}

void preparationPreservesSourceAndBounds() {
  const auto field = fixture();
  const auto beforeHeights = field.heights;
  const auto beforeNormals = field.normals;
  const auto beforeBiomes = field.biomeIndices;
  const auto &chunk = field.chunks.front();
  const auto prepared = prepareTerrainRenderLod(field, chunk);
  assert(prepared);
  assert(prepared->levels[0].empty());
  assert(prepared->geometricErrors[0] == 0.0F);
  assert(prepared->triangleCounts[0] ==
         2 * std::size_t(chunk.cellsX) * std::size_t(chunk.cellsZ));
  const auto first = field.position(chunk.firstCellX, chunk.firstCellZ);
  const auto last = field.position(chunk.firstCellX + chunk.cellsX,
                                   chunk.firstCellZ + chunk.cellsZ);
  assert(prepared->boundsMin.x == first.x && prepared->boundsMin.z == first.y);
  assert(prepared->boundsMax.x == last.x && prepared->boundsMax.z == last.y);
  float minimum = std::numeric_limits<float>::infinity();
  float maximum = -minimum;
  for (int z = chunk.firstCellZ; z <= chunk.firstCellZ + chunk.cellsZ; ++z) {
    for (int x = chunk.firstCellX; x <= chunk.firstCellX + chunk.cellsX; ++x) {
      minimum = std::min(minimum, field.height(x, z));
      maximum = std::max(maximum, field.height(x, z));
    }
  }
  assert(prepared->boundsMin.y == minimum && prepared->boundsMax.y == maximum);
  for (std::size_t level = 1; level < prepared->levels.size(); ++level) {
    const auto detail = static_cast<TerrainLod>(level);
    assert(!prepared->levels[level].empty());
    assert(prepared->geometricErrors[level] ==
           terrainLodGeometricError(field, chunk, detail));
    std::size_t triangles = 0;
    for (const auto &[biome, surface] : prepared->levels[level]) {
      assert(surface.geometry && surface.sampleIndices);
      assert(surface.geometry->vertices.size() ==
             surface.geometry->normals.size());
      assert(surface.geometry->vertices.size() == surface.geometry->uvs.size());
      assert(surface.geometry->vertices.size() ==
             surface.sampleIndices->size());
      assert(surface.geometry->vertices.size() % 3 == 0);
      assert(surface.geometry->revision != 0);
      triangles += surface.geometry->vertices.size() / 3;
    }
    assert(prepared->triangleCounts[level] == triangles);
    assert(triangles ==
           terrainLodSurfaceTriangleCount(chunk.cellsX, chunk.cellsZ, detail));
  }
  assert(field.heights == beforeHeights);
  assert(std::ranges::equal(field.normals, beforeNormals,
                            [](Vec3 first, Vec3 second) {
                              return first.x == second.x &&
                                     first.y == second.y && first.z == second.z;
                            }));
  assert(field.biomeIndices == beforeBiomes);
}

void expandedGeometryRetainsUvsAndRoleSections() {
  const auto field = fixture();
  const auto &chunk = field.chunks.front();
  const auto prepared = prepareTerrainRenderLod(field, chunk);
  for (std::size_t level = 1; level < prepared->levels.size(); ++level) {
    const auto mesh = buildTerrainLodBiomeMesh(
        field, chunk, static_cast<TerrainLod>(level), 0.0F);
    assert(mesh && mesh->skippedBiomes == 0);
    for (const auto &group : mesh->groups) {
      const auto &surface = prepared->levels[level].at(group.biomeId);
      assert(surface.geometry->vertices.size() == group.indices.size());
      const auto biome =
          std::size_t(std::find(field.biomeIds.begin(), field.biomeIds.end(),
                                group.biomeId) -
                      field.biomeIds.begin());
      for (std::size_t vertex = 0; vertex < group.indices.size(); ++vertex) {
        const auto index = group.indices[vertex];
        assert(surface.geometry->vertices[vertex].x == group.vertices[index].x);
        assert(surface.geometry->vertices[vertex].y == group.vertices[index].y);
        assert(surface.geometry->vertices[vertex].z == group.vertices[index].z);
        assert(surface.sampleIndices->at(vertex) == group.sampleIndices[index]);
        const auto position = surface.geometry->vertices[vertex];
        const auto uv = surface.geometry->uvs[vertex];
        assert(std::abs(uv.x - position.x * field.biomeTextureScale(biome)) <
               1e-5F);
        assert(std::abs(uv.y - position.z * field.biomeTextureScale(biome)) <
               1e-5F);
      }
      const auto sections = terrain_detail::buildTerrainMaterialSections(
          field, biome, *surface.sampleIndices);
      assert(surface.sections.indices && sections.indices);
      assert(*surface.sections.indices == *sections.indices);
      assert(surface.sections.sections == sections.sections);
    }
  }
}

void bindingOnlyRebindRetainsGeometryAndIndexIdentity() {
  auto field = fixture();
  const auto original = prepareTerrainRenderLod(field, field.chunks.front());
  auto bindings =
      std::make_shared<TerrainSurfaceMaterials>(*field.surfaceMaterials);
  bindings->bindings["earth"] = "asset://materials/rebound_earth";
  field.surfaceMaterials = bindings;
  const auto rebound = rebindTerrainRenderLod(field, *original);
  assert(rebound->geometricErrors == original->geometricErrors);
  assert(rebound->triangleCounts == original->triangleCounts);
  for (std::size_t level = 1; level < original->levels.size(); ++level) {
    for (const auto &[biome, before] : original->levels[level]) {
      const auto &after = rebound->levels[level].at(biome);
      assert(after.geometry == before.geometry);
      assert(after.sampleIndices == before.sampleIndices);
      assert(after.sections.indices == before.sections.indices);
      assert(after.sections.indexRevision == before.sections.indexRevision);
      for (const auto &section : after.sections.sections) {
        if (section.id == "earth") {
          assert(section.material == "asset://materials/rebound_earth");
        }
      }
    }
  }
}

void addingAndRemovingOverrideRebuildsOnlySections() {
  auto field = fixture();
  const auto original = prepareTerrainRenderLod(field, field.chunks.front());
  field.biomeMaterials = {"asset://materials/override", ""};
  const auto overridden = rebindTerrainRenderLod(field, *original);
  const auto &before = original->levels[1].at("soil");
  const auto &overrideSurface = overridden->levels[1].at("soil");
  assert(overrideSurface.geometry == before.geometry);
  assert(overrideSurface.sections.sections.empty());
  assert(!overrideSurface.sections.indices);
  field.biomeMaterials.clear();
  const auto restored = rebindTerrainRenderLod(field, *overridden);
  const auto &after = restored->levels[1].at("soil");
  assert(after.geometry == before.geometry &&
         after.sampleIndices == before.sampleIndices);
  assert(after.sections.indices);
  assert(*after.sections.indices == *before.sections.indices);
  assert(after.sections.sections == before.sections.sections);
}

void canceledPreparationPublishesNothing() {
  const auto field = fixture();
  std::stop_source cancellation;
  cancellation.request_stop();
  assert(!prepareTerrainRenderLod(field, field.chunks.front(),
                                  cancellation.get_token()));
}

} // namespace

int main() {
  preparationPreservesSourceAndBounds();
  expandedGeometryRetainsUvsAndRoleSections();
  bindingOnlyRebindRetainsGeometryAndIndexIdentity();
  addingAndRemovingOverrideRebuildsOnlySections();
  canceledPreparationPublishesNothing();
  std::cout << "Prepared terrain render LOD checks passed\n";
}
