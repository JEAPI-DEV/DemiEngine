#include "demi/assets/TerrainAsset.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ModelCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainUpdate.h"
#include "demi/runtime/terrain/TerrainWaterMesh.h"
#include "demi/runtime/terrain/TerrainWaterQueries.h"
#include "demi/runtime/terrain/TerrainWorld.h"
#include "demi/runtime/terrain/TerrainWorldBatch.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

using namespace demi::runtime;
using Json = nlohmann::json;

namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}

TerrainRecipe recipe() {
  TerrainRecipe settings;
  settings.size = {16, 16};
  settings.cellsX = settings.cellsZ = 16;
  settings.chunkCells = 8;
  settings.landforms.at("default").baseHeight = 3;
  settings.landforms.at("default").heightVariation = 0;
  settings.graph = defaultTerrainGraph();
  settings.graph["nodes"].push_back({{"id", "lake"},
                                     {"type", "water"},
                                     {"parameters",
                                      {{"kind", "lake"},
                                       {"level", 5},
                                       {"center_x", 8},
                                       {"center_z", 8},
                                       {"radius", 5},
                                       {"shallow_color", {0.1, 0.8, 0.2, 0.2}},
                                       {"deep_color", {0, 0.2, 0.1, 0.9}},
                                       {"absorption_distance", 1.5},
                                       {"roughness", 0.4}}}});
  settings.graph["links"][0]["from"]["node"] = "lake";
  settings.graph["links"].push_back(
      {{"id", "landform_lake"},
       {"from", {{"node", "landform"}, {"port", "field"}}},
       {"to", {{"node", "lake"}, {"port", "field"}}}});
  settings.graph["links"].push_back(
      {{"id", "lake_surface"},
       {"from", {{"node", "lake"}, {"port", "water"}}},
       {"to", {{"node", "terrain_output"}, {"port", "water"}}}});
  return settings;
}

void addOwner(World &world, const std::string &id,
              const TerrainRecipe &settings, bool asset = false) {
  Entity owner;
  owner.id = id;
  owner.setComponent(Transform3DComponent{.position = {10, 2, 30}});
  Terrain3DComponent terrain;
  if (asset)
    terrain.asset = "asset://terrain/water";
  else
    terrain.recipe = settings.toJson();
  owner.setComponent(std::move(terrain));
  world.entities.push_back(std::move(owner));
}

const MeshRendererComponent &water(const World &world,
                                   const std::string &owner) {
  const auto *entity = findEntity(world, owner + "/__water/lake");
  require(entity && terrainSurfaceOwner(*entity) == owner,
          "Water was not published under its authored terrain owner");
  require(!entity->hasComponent<ModelCollider3DComponent>(),
          "Water became a solid collider");
  require(entity->component<Transform3DComponent>()->parent == owner,
          "Water lost terrain-local parenting");
  const auto *mesh = entity->component<MeshRendererComponent>();
  require(mesh && !mesh->vertices.empty() && mesh->surfaceMode == "transparent",
          "Water has no drawable transparent surface");
  return *mesh;
}

void sourceAndPreparedLoading() {
  const auto settings = recipe();
  World source;
  addOwner(source, "source", settings);
  std::string error;
  require(materializeTerrains(source, error), error.c_str());
  const auto vertices = water(source, "source").vertices;
  const auto colors = water(source, "source").vertexColors;
  const auto count = source.entities.size();
  require(materializeTerrains(source, error), error.c_str());
  require(source.entities.size() == count, "Loading duplicated water children");
  auto field =
      findEntity(source, "source")->component<Terrain3DComponent>()->generated;
  const auto payload =
      demi::assets::serializeTerrainAssetPayload(*field, "recipe", "inputs");
  auto decoded = demi::assets::deserializeTerrainAssetPayload(payload);
  World prepared;
  addOwner(prepared, "prepared", settings, true);
  const TerrainFieldResolver resolver = [&](std::string_view, Json &document) {
    document = nullptr;
    return decoded;
  };
  const TerrainInputResolver forbidden =
      [](const TerrainRecipe &) -> TerrainGenerationInputs {
    throw std::runtime_error("Prepared water attempted procedural generation");
  };
  require(materializeTerrains(prepared, error, nullptr, forbidden, resolver),
          error.c_str());
  const auto &mesh = water(prepared, "prepared");
  require(mesh.roughness == 0.4F && mesh.vertexColors.size() == colors.size(),
          "Prepared water lost authored appearance");
  for (std::size_t i = 0; i < colors.size(); ++i)
    require(mesh.vertexColors[i].r == colors[i].r &&
                mesh.vertexColors[i].g == colors[i].g &&
                mesh.vertexColors[i].b == colors[i].b &&
                mesh.vertexColors[i].a == colors[i].a,
            "Prepared water changed its depth colours");
  require(mesh.vertices.size() == vertices.size(),
          "Prepared water geometry changed");
  for (std::size_t i = 0; i < vertices.size(); ++i)
    require(mesh.vertices[i].x == vertices[i].x &&
                mesh.vertices[i].y == vertices[i].y &&
                mesh.vertices[i].z == vertices[i].z,
            "Cooked water did not retain surface positions");
  findEntity(prepared, "prepared")->enabled = false;
  synchronizeTerrainVisibility(prepared);
  require(!findEntity(prepared, "prepared/__water/lake")->enabled,
          "Disabled terrain left its water visible");
  prepared.entities.erase(prepared.entities.begin());
  require(materializeTerrains(prepared, error, nullptr, forbidden, resolver) &&
              prepared.entities.empty(),
          "Deleting terrain left water children behind");
}

void sharedUpdatesAndRollback() {
  auto settings = recipe();
  World world;
  addOwner(world, "first", settings);
  addOwner(world, "second", settings);
  std::string error;
  require(materializeTerrains(world, error), error.c_str());
  const auto oldRevision = water(world, "first").revision;
  auto before =
      findEntity(world, "first")->component<Terrain3DComponent>()->generated;
  auto appearance = settings;
  appearance.biomes.at("default").color = {1, 0, 0, 1};
  auto tint = updateTerrain(settings, appearance, before);
  require(bool(tint), "Could not prepare tint update");
  const std::array<std::string, 2> owners{"first", "second"};
  require(
      updateTerrainWorldBatch(world, owners, appearance.toJson(), *tint, error),
      error.c_str());
  require(water(world, "first").revision == oldRevision,
          "Biome-only edit rebuilt unrelated water geometry");
  settings = appearance;
  before =
      findEntity(world, "first")->component<Terrain3DComponent>()->generated;
  auto raised = settings;
  raised.graph["nodes"].back()["parameters"]["level"] = 7;
  auto update = updateTerrain(settings, raised, before);
  require(bool(update), "Could not prepare water level update");
  require(
      updateTerrainWorldBatch(world, owners, raised.toJson(), *update, error),
      error.c_str());
  require(water(world, "first").vertices.front().y == 7 &&
              water(world, "second").vertices.front().y == 7,
          "Shared terrain placements did not update their water levels");
  auto restored = updateTerrain(raised, settings, update->field);
  require(bool(restored), "Could not prepare water restore");
  auto preparation = prepareTerrainWorldUpdate(
      world, "first", settings.toJson(), *restored, error);
  require(bool(preparation), error.c_str());
  std::stop_source cancel;
  cancel.request_stop();
  require(!publishTerrainWorldUpdate(world, std::move(*preparation), error,
                                     cancel.get_token()),
          "Cancelled water publication was committed");
  require(water(world, "first").vertices.front().y == 7,
          "Cancelled update changed water");
  require(updateTerrainWorldBatch(world, owners, settings.toJson(), *restored,
                                  error),
          error.c_str());
  require(water(world, "first").vertices.front().y == 5,
          "Restoring terrain history did not restore water");
  auto dry = settings;
  dry.graph = defaultTerrainGraph();
  auto removed = updateTerrain(settings, dry, restored->field);
  require(bool(removed), "Could not prepare dry terrain");
  require(updateTerrainWorldBatch(world, owners, dry.toJson(), *removed, error),
          error.c_str());
  require(!findEntity(world, "first/__water/lake") &&
              !findEntity(world, "second/__water/lake"),
          "Removing water output left stale visible surfaces");
}

void failedSharedWaterAdditionIsAtomic() {
  auto dry = recipe();
  dry.graph = defaultTerrainGraph();
  World world;
  addOwner(world, "first", dry);
  addOwner(world, "second", dry);
  std::string error;
  require(materializeTerrains(world, error), error.c_str());
  const auto before =
      findEntity(world, "first")->component<Terrain3DComponent>()->generated;
  Entity conflict;
  conflict.id = "second/__water/lake";
  conflict.name = "Authored entity must survive";
  world.entities.push_back(std::move(conflict));
  const auto count = world.entities.size();
  const auto wet = recipe();
  auto update = updateTerrain(dry, wet, before);
  require(bool(update), "Could not prepare water addition");
  const std::array<std::string, 2> owners{"first", "second"};
  require(!updateTerrainWorldBatch(world, owners, wet.toJson(), *update, error),
          "Water ID conflict was accepted");
  require(world.entities.size() == count &&
              !findEntity(world, "first/__water/lake") &&
              findEntity(world, "second/__water/lake")->name ==
                  "Authored entity must survive" &&
              findEntity(world, "first")
                      ->component<Terrain3DComponent>()
                      ->generated == before &&
              findEntity(world, "second")
                      ->component<Terrain3DComponent>()
                      ->generated == before,
          "Failed later placement partially published water or terrain");
}
} // namespace

void exampleWaterIsPublishedAtItsAuthoredCoordinates() {
  const auto source =
      std::filesystem::path(__FILE__).parent_path().parent_path() /
      "examples/terrain_graph_3d/assets/terrain/landscape.terrain.json";
  std::ifstream input(source);
  require(bool(input), "Could not read actual terrain graph example");
  const auto document = Json::parse(input);
  const auto settings = TerrainRecipe::parse(document.at("recipe"));
  auto generated = executeTerrainGraph(settings);
  require(bool(generated), "Could not evaluate actual terrain graph example");
  Entity owner;
  owner.id = "terrain";
  owner.setComponent(Transform3DComponent{.position = {-64, 0, -64}});
  auto meshes = buildTerrainWaterMeshes(owner, *generated);
  require(!meshes.empty(), "Actual terrain graph example published no water");
  std::size_t lakeVertices = 0, riverVertices = 0;
  for (const auto &entry : meshes) {
    const auto *mesh = entry.component<MeshRendererComponent>();
    if (entry.id.ends_with("/basin_lake")) {
      lakeVertices = mesh->vertices.size();
      require(mesh->boundsMin.x < 991 && mesh->boundsMax.x > 991 &&
                  mesh->boundsMin.z < 973 && mesh->boundsMax.z > 973,
              "Lake is not at the sculpted basin");
    }
    if (entry.id.ends_with("/outlet_river"))
      riverVertices = mesh->vertices.size();
    std::cout << entry.id << ": " << mesh->vertices.size()
              << " vertices; local bounds " << mesh->boundsMin.x << ','
              << mesh->boundsMin.y << ',' << mesh->boundsMin.z << " to "
              << mesh->boundsMax.x << ',' << mesh->boundsMax.y << ','
              << mesh->boundsMax.z << '\n';
  }
  require(lakeVertices > 6 && riverVertices > 0,
          "Relocated example did not produce a lake and river surface");
  const auto &artifacts = *generated->graphArtifacts;
  const auto sample = sampleTerrainWaterAt(
      artifacts.water, *artifacts.waterResult, nullptr, {991, 0, 973},
      generated->cellsX, generated->cellsZ, generated->size);
  require(sample && sample->submerged && sample->depth > 0,
          "The sculpted basin centre is still dry");
  std::cout << "Basin centre water depth: " << sample->depth << '\n';
}

int main() {
  try {
    sourceAndPreparedLoading();
    sharedUpdatesAndRollback();
    failedSharedWaterAdditionIsAtomic();
    exampleWaterIsPublishedAtItsAuthoredCoordinates();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
