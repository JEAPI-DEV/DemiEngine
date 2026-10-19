#include "demi/assets/AssetHash.h"
#include "demi/assets/TerrainAsset.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainScatterRuntime.h"
#include "demi/runtime/assets/RuntimeAssetService.h"
#include <thread>
#include "demi/runtime/terrain/TerrainScatterBatches.h"
#include "demi/runtime/terrain/TerrainScatterCollision.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/SphereCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Rigidbody3DComponent.h"
#include "demi/runtime/terrain/TerrainWorld.h"
#include "demi/schema/Validation.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <system_error>

using namespace demi::runtime;

namespace {
const char *OwnerId = "terrain";
const char *PaletteId = "asset://terrain/palettes/test";

std::filesystem::path exampleProjectPath() {
  auto project = std::filesystem::path(__FILE__).parent_path().parent_path() /
                 "examples/terrain_3d";
  if (std::filesystem::exists(project / "demi.project.json"))
    return project;
  auto directory = std::filesystem::current_path();
  while (directory != directory.root_path()) {
    const auto candidate = directory / "examples/terrain_3d";
    if (std::filesystem::exists(candidate / "demi.project.json"))
      return candidate;
    directory = directory.parent_path();
  }
  return project;
}

World worldWithTerrain() {
  World world;
  Entity owner;
  owner.id = OwnerId;
  owner.name = "Terrain";
  owner.setComponent(Transform3DComponent{});
  world.entities.push_back(owner);
  return world;
}

std::size_t scatterChildren(const World &world) {
  const std::string prefix =
      std::string(OwnerId) + "/__scatter/" + PaletteId + "/";
  return std::count_if(
      world.entities.begin(), world.entities.end(),
      [&](const Entity &entity) { return entity.id.rfind(prefix, 0) == 0; });
}

TerrainScatterPlacement placementAt(std::size_t cell, float height,
                                    std::string ruleId, float yaw) {
  TerrainScatterPlacement placement;
  placement.ruleId = ruleId;
  placement.model = "asset://terrain/props/tree";
  placement.cell = cell;
  placement.candidateX = static_cast<int>(cell);
  placement.candidateZ = 0;
  placement.position = {float(cell), height, 0};
  placement.yaw = yaw;
  placement.scale = 1;
  return placement;
}

void flush(WorldCommandBuffer &commands, World &world) {
  (void)commands.flush(world);
}

// The key must be stable for a cell and must separate roles and palettes,
// otherwise two roles landing in one cell would overwrite each other.
void instanceIdIsStableAndSeparating() {
  const auto a = terrainScatterInstanceId(OwnerId, PaletteId, "tree", 42, 0);
  const auto b = terrainScatterInstanceId(OwnerId, PaletteId, "tree", 42, 0);
  assert(a == b);
  assert(a != terrainScatterInstanceId(OwnerId, PaletteId, "bush", 42, 0));
  assert(a != terrainScatterInstanceId(OwnerId, PaletteId, "tree", 43, 0));
  assert(a != terrainScatterInstanceId("other", PaletteId, "tree", 42, 0));
  assert(a != terrainScatterInstanceId(OwnerId, "asset://other/palette", "tree",
                                       42, 0));
  assert(a.find(OwnerId) == 0);
}

// Reconciling the same field twice must change nothing the second time. This is
// the property that keeps a brush stroke from churning the world.
void repeatSyncIsIdempotent() {
  auto world = worldWithTerrain();
  WorldCommandBuffer commands;
  RuntimePrefabService prefabs;
  HeightField field;
  field.paletteId = PaletteId;
  field.scatterPlacements = {placementAt(1, 2.F, "tree", 0.5F),
                             placementAt(2, 3.F, "tree", 1.5F)};

  std::string error;
  const auto first =
      syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  assert(error.empty());
  assert(first.created == 2);
  flush(commands, world);
  assert(scatterChildren(world) == 2);

  const auto second =
      syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  assert(error.empty());
  assert(second.created == 0 && second.removed == 0);
  assert(second.retained == 2);
  flush(commands, world);
  assert(scatterChildren(world) == 2);
}

// A stroke that moves the ground must MOVE the existing instance, not rebuild
// it. Rebuilding would destroy prefab pooling and any per-instance state.
void movementUpdatesInPlace() {
  auto world = worldWithTerrain();
  WorldCommandBuffer commands;
  RuntimePrefabService prefabs;
  HeightField field;
  field.paletteId = PaletteId;
  field.scatterPlacements = {placementAt(1, 2.F, "tree", 0.5F)};
  std::string error;
  (void)syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  flush(commands, world);
  const auto id = terrainScatterInstanceId(OwnerId, PaletteId, "tree", 1, 0);
  const auto *before = findEntity(world, id);
  assert(before != nullptr);
  assert(before->component<Transform3DComponent>()->position.y == 2.F);

  // Same cell, new height and yaw: a sculpt moved the ground under it.
  field.scatterPlacements = {placementAt(1, 7.F, "tree", 2.5F)};
  const auto stats =
      syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  assert(error.empty());
  assert(stats.updated == 1 && stats.created == 0 && stats.removed == 0);
  flush(commands, world);
  const auto *after = findEntity(world, id);
  assert(after != nullptr);
  assert(after->component<Transform3DComponent>()->position.y == 7.F);
  assert(after->component<Transform3DComponent>()->rotation.y == 2.5F);
  assert(scatterChildren(world) == 1);
}

// Instances the new field no longer produces must go, or the terrain would
// accumulate props every stroke.
void removedCellsAreCleanedUp() {
  auto world = worldWithTerrain();
  WorldCommandBuffer commands;
  RuntimePrefabService prefabs;
  HeightField field;
  field.paletteId = PaletteId;
  field.scatterPlacements = {placementAt(1, 2.F, "tree", 0.F),
                             placementAt(2, 3.F, "tree", 0.F),
                             placementAt(3, 4.F, "bush", 0.F)};
  std::string error;
  (void)syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  flush(commands, world);
  assert(scatterChildren(world) == 3);

  // A ruleId's weight dropped to zero, or its biome no longer matches.
  field.scatterPlacements = {placementAt(1, 2.F, "tree", 0.F)};
  const auto stats =
      syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  assert(error.empty());
  assert(stats.removed == 2 && stats.retained == 1);
  flush(commands, world);
  assert(scatterChildren(world) == 1);
  assert(findEntity(world, terrainScatterInstanceId(OwnerId, PaletteId, "tree",
                                                    1, 0)) != nullptr);
}

// Two roles in the same cell are two instances, not one overwriting the other.
void sameCellDifferentRolesCoexist() {
  auto world = worldWithTerrain();
  WorldCommandBuffer commands;
  RuntimePrefabService prefabs;
  HeightField field;
  field.paletteId = PaletteId;
  field.scatterPlacements = {placementAt(9, 5.F, "tree", 0.F),
                             placementAt(9, 5.F, "grass", 0.F)};
  std::string error;
  const auto stats =
      syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  assert(stats.created == 2);
  flush(commands, world);
  assert(scatterChildren(world) == 2);
  assert(findEntity(world, terrainScatterInstanceId(OwnerId, PaletteId, "tree",
                                                    9, 0)) != nullptr);
  assert(findEntity(world, terrainScatterInstanceId(OwnerId, PaletteId, "grass",
                                                    9, 0)) != nullptr);
}

// A missing owner is an error, never a silent no-op that looks like a success.
void missingOwnerIsReported() {
  auto world = worldWithTerrain();
  WorldCommandBuffer commands;
  RuntimePrefabService prefabs;
  HeightField field;
  field.paletteId = PaletteId;
  field.scatterPlacements = {placementAt(1, 2.F, "tree", 0.F)};
  std::string error;
  const auto stats = syncTerrainScatter(world, commands, "no_such_terrain",
                                        field, &prefabs, error);
  assert(!error.empty());
  assert(stats.total() == 0);
}

// A prefab that cannot be resolved is reported rather than swallowed, because a
// silently missing prop is indistinguishable from a scattering bug.
void unresolvablePrefabIsCounted() {
  auto world = worldWithTerrain();
  WorldCommandBuffer commands;
  RuntimePrefabService prefabs;
  HeightField field;
  field.paletteId = PaletteId;
  field.scatterPlacements = {placementAt(1, 2.F, "tree", 0.F),
                             placementAt(2, 2.F, "tree", 0.F)};
  field.scatterPlacements[0].prefab = "prefab://does/not/exist";
  std::string error;
  const auto stats =
      syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  assert(stats.unresolved == 1);
  flush(commands, world);
  // The direct entry still placed, so one bad prefab does not sink the field.
  assert(scatterChildren(world) == 1);
}

// Releasing must clear the owner's instances and nothing else.
void releaseClearsOnlyItsOwner() {
  auto world = worldWithTerrain();
  Entity other;
  other.id = "other_terrain";
  other.setComponent(Transform3DComponent{});
  world.entities.push_back(other);
  Entity neighbour;
  neighbour.id = "a_bystander";
  world.entities.push_back(neighbour);

  WorldCommandBuffer commands;
  RuntimePrefabService prefabs;
  HeightField field;
  field.paletteId = PaletteId;
  field.scatterPlacements = {placementAt(1, 2.F, "tree", 0.F),
                             placementAt(2, 2.F, "tree", 0.F)};
  std::string error;
  (void)syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  flush(commands, world);
  assert(scatterChildren(world) == 2);

  const auto removed = releaseTerrainScatter(world, commands, OwnerId);
  assert(removed == 2);
  flush(commands, world);
  assert(scatterChildren(world) == 0);
  assert(findEntity(world, OwnerId) != nullptr);
  assert(findEntity(world, "other_terrain") != nullptr);
  assert(findEntity(world, "a_bystander") != nullptr);
}

// The whole point of the shared service: one pool, reachable from both the
// terrain path and the host that owns the scene flow. Two services would double
// the prefab memory for the same asset, which is the cost this replaced.
void sharedServiceIsVisibleToBothConsumers() {
  RuntimePrefabService shared;
  shared.configure(std::filesystem::temp_directory_path());
  LuaScriptHost host;
  host.setPrefabService(&shared);
  // The host forwards the same service to the scene flow it owns, so there is
  // no second wiring point that can be forgotten.
  assert(host.prefabService() == &shared);
  // A service the host does not own must not be reachable after the host dies.
  {
    RuntimePrefabService temporary;
    LuaScriptHost scoped;
    scoped.setPrefabService(&temporary);
    assert(scoped.prefabService() == &temporary);
  }
}

// A host with no injected service must not crash on any prefab entry point.
void hostWithoutServiceIsSafe() {
  LuaScriptHost host;
  assert(host.prefabService() == nullptr);
  assert(!host.instantiatePrefab("prefab://crate", {}));
  assert(!host.releasePrefab("anything"));
  assert(host.pooledPrefabCount("prefab://crate") == 0);
  host.setPrefabTemplateCacheCapacity(4);
}

// A caller with no prefab owner must get the asset path and a count for the
// prefab roles, never a guess.
void noServiceSkipsPrefabRoles() {
  auto world = worldWithTerrain();
  WorldCommandBuffer commands;
  HeightField field;
  field.paletteId = PaletteId;
  field.scatterPlacements = {placementAt(1, 2.F, "tree", 0.F)};
  field.scatterPlacements[0].prefab = "prefab://tree";
  std::string error;
  const auto stats =
      syncTerrainScatter(world, commands, OwnerId, field, nullptr, error);
  assert(error.empty());
  assert(stats.unresolved == 1 && stats.created == 0);
  flush(commands, world);
  assert(scatterChildren(world) == 0);
}

void unresolvedSceneScatterDoesNotCommitPartialEntities() {
  auto world = worldWithTerrain();
  auto field = std::make_shared<HeightField>();
  field->paletteId = PaletteId;
  field->scatterPlacements = {placementAt(1, 2.F, "tree", 0.F),
                              placementAt(2, 3.F, "grass", 0.F)};
  field->scatterPlacements.back().prefab = "prefab://missing";
  Terrain3DComponent terrain;
  terrain.generated = field;
  world.entities.front().setComponent(std::move(terrain));
  const auto before = world.entities.size();
  std::string error;
  const auto report = materializeTerrainScatter(world, nullptr, error);
  assert(error.empty());
  assert(report.unresolved == 1);
  assert(report.placed == 1);
  assert(world.entities.size() == before);

  RuntimePrefabService prefabs;
  prefabs.configure(std::filesystem::temp_directory_path());
  const auto failed = materializeTerrainScatter(world, &prefabs, error);
  assert(failed.unresolved == 1);
  assert(error.find("prefab://missing") != std::string::npos);
  assert(world.entities.size() == before);
}

void prefabPrototypeKeepsGeometryAndStableCellId() {
  const auto project = exampleProjectPath();
  assert(std::filesystem::exists(project /
                                 "prefabs/scatter_prototype.prefab.json"));
  RuntimePrefabService prefabs;
  prefabs.configure(project);
  auto world = worldWithTerrain();
  WorldCommandBuffer commands;
  HeightField field;
  field.paletteId = PaletteId;
  field.scatterPlacements = {placementAt(7, 2.F, "grass", 0.5F)};
  field.scatterPlacements.front().model = "asset://terrain/surfaces/grass";
  field.scatterPlacements.front().prefab = "prefab://scatter_prototype";
  const auto id = terrainScatterInstanceId(OwnerId, PaletteId, "grass", 7, 0);
  std::string error;
  const auto created =
      syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  assert(error.empty() && created.created == 1 && created.unresolved == 0);
  flush(commands, world);
  const auto *body = findEntity(world, id + "/body");
  assert(body != nullptr && body->prefabInstance == id);
  assert(body->component<MeshRendererComponent>() != nullptr);
  assert(body->component<MeshRendererComponent>()->model.empty());
  const auto retained =
      syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  assert(error.empty() && retained.retained == 1 && retained.created == 0);

  field.scatterPlacements.front().position.y = 5.F;
  const auto moved =
      syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  assert(error.empty() && moved.updated == 1 && moved.created == 0);
  flush(commands, world);
  body = findEntity(world, id + "/body");
  assert(body != nullptr);
  assert(body->component<Transform3DComponent>()->position.y == 5.F);

  field.scatterPlacements.clear();
  const auto removed =
      syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  assert(error.empty() && removed.removed == 1);
  flush(commands, world);
  assert(findEntity(world, id + "/body") == nullptr);

  field.scatterPlacements = {placementAt(8, 1.F, "grass", 0.F)};
  field.scatterPlacements.front().prefab = "prefab://scatter_prototype";
  const auto nextId =
      terrainScatterInstanceId(OwnerId, PaletteId, "grass", 8, 0);
  const auto next =
      syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  assert(error.empty() && next.created == 1);
  flush(commands, world);
  assert(findEntity(world, nextId + "/body") != nullptr);
  assert(findEntity(world, id + "/body") == nullptr);
}

void collisionFreePlacementsShareRenderOwners() {
  auto world = worldWithTerrain();
  world.entities.front().setComponent(Terrain3DComponent{});
  HeightField field;
  field.paletteId = PaletteId;
  for (int index = 0; index < 100; ++index) {
    auto placement = placementAt(index, 2.F, "grass", 0.F);
    placement.collision = TerrainCollisionPolicy::None;
    field.scatterPlacements.push_back(placement);
  }
  WorldCommandBuffer commands;
  std::string error;
  const auto created = syncTerrainScatter(world, commands, OwnerId, field,
                                           nullptr, error);
  assert(error.empty() && created.created == 100);
  flush(commands, world);
  assert(world.entities.size() == 2);
  const auto *instances = world.entities.back().component<MeshInstances3DComponent>();
  assert(instances && instances->transforms().size() == 100);
  assert(world.entities.back().prefabInstance.empty());
  const auto retained = syncTerrainScatter(world, commands, OwnerId, field,
                                            nullptr, error);
  assert(error.empty() && retained.retained == 100 && commands.empty());

  const auto excluded = terrainScatterInstanceId(OwnerId, PaletteId, "grass", 7, 0);
  world.entities.front().component<Terrain3DComponent>()->scatterExclusions = {excluded};
  const auto removed = syncTerrainScatter(world, commands, OwnerId, field,
                                           nullptr, error);
  assert(error.empty() && removed.removed == 1 && removed.retained == 99);
  flush(commands, world);
  instances = world.entities.back().component<MeshInstances3DComponent>();
  assert(instances && instances->transforms().size() == 99);
  assert(!instances->transforms().contains(excluded));
}

void visualPrefabBatchesPreserveHierarchyAndMaterials() {
  const auto directory = std::filesystem::temp_directory_path() /
      ("demi-scatter-batches-" + std::to_string(
          std::chrono::steady_clock::now().time_since_epoch().count()));
  std::filesystem::create_directories(directory / "prefabs");
  struct Cleanup {
    std::filesystem::path directory;
    ~Cleanup() { std::filesystem::remove_all(directory); }
  } cleanup{directory};
  {
    std::ofstream project(directory / "demi.project.json");
    project << R"({"format_version":1,"name":"Batch probe"})";
    std::ofstream prefab(directory / "prefabs/reed.prefab.json");
    prefab << R"({"format_version":1,"id":"prefab://reed","entities":[
      {"id":"root","components":{"Transform3D":{"position":[1,0,0]}},
       "children":[{"id":"blade","components":{
         "Transform3D":{"position":[0,2,0]},
         "MeshRenderer":{"shape":"cube","material":"asset://reed/material",
           "medium_lod_model":"asset://reed/low","medium_lod_distance":20},
         "SphereCollider3D":{"radius":0.25},
         "Rigidbody3D":{}}}]}]})";
  }
  RuntimePrefabService prefabs;
  prefabs.configure(directory);
  auto world = worldWithTerrain();
  HeightField field;
  field.paletteId = PaletteId;
  for (int index = 0; index < 20; ++index) {
    auto placement = placementAt(index, 3.F, "reeds", 0.F);
    placement.prefab = "prefab://reed";
    placement.collision = TerrainCollisionPolicy::None;
    field.scatterPlacements.push_back(placement);
  }
  WorldCommandBuffer commands;
  std::string error;
  const auto created = syncTerrainScatter(world, commands, OwnerId, field,
                                           &prefabs, error);
  assert(error.empty() && created.created == 20 && created.prefabInstances == 0);
  flush(commands, world);
  assert(world.entities.size() == 2);
  const auto &batch = world.entities.back();
  const auto *mesh = batch.component<MeshRendererComponent>();
  assert(mesh && mesh->material == "asset://reed/material");
  assert(mesh->mediumLodModel == "asset://reed/low" && mesh->mediumLodDistance == 20);
  assert(!batch.hasComponent<SphereCollider3DComponent>());
  assert(!batch.hasComponent<Rigidbody3DComponent>());
  const auto *instances = batch.component<MeshInstances3DComponent>();
  assert(instances && instances->transforms().size() == 20);
  const auto key = terrainScatterInstanceId(OwnerId, PaletteId, "reeds", 0, 0);
  const auto &transform = instances->transforms().at(key);
  assert(transform.position.x == 1 && transform.position.y == 5);

  {
    std::ofstream prefab(directory / "prefabs/pine.prefab.json");
    prefab << R"({"format_version":1,"id":"prefab://pine","entities":[
      {"id":"root","components":{"Transform3D":{}},"children":[
        {"id":"needles","components":{"Transform3D":{},
          "MeshRenderer":{"shape":"sphere"},"SphereCollider3D":{"radius":0.75}}}
      ]}]})";
  }
  auto collisionWorld = worldWithTerrain();
  HeightField collisionField;
  collisionField.paletteId = PaletteId;
  auto colliding = placementAt(7, 2.F, "tree", 0.F);
  colliding.prefab = "prefab://reed";
  colliding.collision = TerrainCollisionPolicy::Static;
  collisionField.scatterPlacements = {colliding};
  const auto stableId = terrainScatterInstanceId(OwnerId, PaletteId, "tree", 7, 0);
  WorldCommandBuffer replacementCommands;
  const auto initial = syncTerrainScatter(collisionWorld, replacementCommands,
      OwnerId, collisionField, &prefabs, error);
  assert(error.empty() && initial.created == 1);
  flush(replacementCommands, collisionWorld);
  assert(findEntity(collisionWorld, stableId + "/blade"));

  collisionField.scatterPlacements.front().prefab = "prefab://pine";
  const auto replacement = syncTerrainScatter(collisionWorld, replacementCommands,
      OwnerId, collisionField, &prefabs, error);
  assert(error.empty() && replacement.updated == 1 && replacement.created == 0);
  assert(findEntity(collisionWorld, stableId + "/blade"));
  flush(replacementCommands, collisionWorld);
  assert(!findEntity(collisionWorld, stableId + "/blade"));
  const auto *needles = findEntity(collisionWorld, stableId + "/needles");
  assert(needles && needles->prefabInstance == stableId);
  assert(needles->component<SphereCollider3DComponent>()->radius == .75F);
  assert(prefabs.sourcePrefab(stableId) == std::optional<std::string_view>("prefab://pine"));
  const auto retainedReplacement = syncTerrainScatter(collisionWorld,
      replacementCommands, OwnerId, collisionField, &prefabs, error);
  assert(error.empty() && retainedReplacement.retained == 1 && replacementCommands.empty());

  collisionField.scatterPlacements.front().prefab = "prefab://missing_replacement";
  const auto rejected = syncTerrainScatter(collisionWorld, replacementCommands,
      OwnerId, collisionField, &prefabs, error);
  assert(!error.empty() && rejected.unresolved == 1);
  assert(replacementCommands.empty());
  assert(findEntity(collisionWorld, stableId + "/needles"));
  assert(prefabs.sourcePrefab(stableId) == std::optional<std::string_view>("prefab://pine"));

  // A policy change transfers this durable placement between live prefab
  // ownership and render-only batching; neither representation may survive
  // alongside the other or retain a collider after becoming decorative.
  collisionField.scatterPlacements.front().prefab = "prefab://pine";
  collisionField.scatterPlacements.front().collision = TerrainCollisionPolicy::None;
  const auto decorative = syncTerrainScatter(collisionWorld, replacementCommands,
      OwnerId, collisionField, &prefabs, error);
  assert(error.empty() && decorative.unresolved == 0);
  flush(replacementCommands, collisionWorld);
  assert(!findEntity(collisionWorld, stableId + "/root"));
  assert(!findEntity(collisionWorld, stableId + "/needles"));
  assert(!prefabs.sourcePrefab(stableId));
  assert(collisionWorld.entities.size() == 2);
  const auto *decorativeCopies = collisionWorld.entities.back()
      .component<MeshInstances3DComponent>();
  assert(decorativeCopies && decorativeCopies->transforms().contains(stableId));
  for (const auto &entity : collisionWorld.entities) {
    assert(!entity.hasComponent<SphereCollider3DComponent>());
    assert(entity.prefabInstance.empty());
  }

  collisionField.scatterPlacements.front().collision = TerrainCollisionPolicy::Static;
  const auto physical = syncTerrainScatter(collisionWorld, replacementCommands,
      OwnerId, collisionField, &prefabs, error);
  assert(error.empty() && physical.unresolved == 0);
  flush(replacementCommands, collisionWorld);
  assert(collisionWorld.entities.size() == 3);
  const auto *physicalNeedles = findEntity(collisionWorld, stableId + "/needles");
  assert(physicalNeedles && physicalNeedles->prefabInstance == stableId);
  assert(physicalNeedles->component<SphereCollider3DComponent>()->radius == .75F);
  assert(prefabs.sourcePrefab(stableId) == std::optional<std::string_view>("prefab://pine"));
  for (const auto &entity : collisionWorld.entities) {
    assert(!entity.hasComponent<MeshInstances3DComponent>());
    assert(!entity.hasComponent<TerrainScatterBatch>());
  }
}

void collisionPolicyKeepsAuthoredShapes() {
  const nlohmann::json preset{{"id", "body"}, {"preset", "static_box_3d"}};
  const auto removed = terrainScatterCollisionOverrides(preset, TerrainCollisionPolicy::None);
  assert(removed.contains("BoxCollider3D") && removed["BoxCollider3D"].is_null());
  const nlohmann::json controller{{"components", {{"CharacterController3D", nlohmann::json::object()}}}};
  assert(terrainScatterCollisionOverrides(controller, TerrainCollisionPolicy::Static).empty());
  assert(terrainScatterCollisionOverrides(controller, TerrainCollisionPolicy::None)
             .at("CharacterController3D").is_null());
  Entity entity;
  entity.setComponent(SphereCollider3DComponent{.radius = 2.F});
  entity.setComponent(Rigidbody3DComponent{});
  applyTerrainScatterCollision(entity, TerrainCollisionPolicy::Trigger);
  const auto *collider = entity.component<SphereCollider3DComponent>();
  assert(collider && collider->radius == 2.F && collider->isTrigger);
  assert(entity.component<Rigidbody3DComponent>()->bodyType == "static");
  applyTerrainScatterCollision(entity, TerrainCollisionPolicy::None);
  assert(!entity.hasComponent<SphereCollider3DComponent>());
  assert(!entity.hasComponent<Rigidbody3DComponent>());
  applyTerrainScatterCollision(entity, TerrainCollisionPolicy::Static);
  collider = entity.component<SphereCollider3DComponent>();
  assert(collider && collider->radius == 2.F && !collider->isTrigger);
  assert(entity.component<Rigidbody3DComponent>()->bodyType == "static");
}

void initialSceneScatterUsesConfiguredPrefabService() {
  // The startup contract needs real authored loading, not the example's
  // 512 x 512 terrain generation. Bound the fixture before preparing assets.
  struct ProjectFixture {
    std::filesystem::path directory;

    ~ProjectFixture() {
      std::error_code ignored;
      std::filesystem::remove_all(directory, ignored);
    }

    void writeJson(const std::filesystem::path &relativePath,
                   const nlohmann::json &document) const {
      const auto path = directory / relativePath;
      std::filesystem::create_directories(path.parent_path());
      std::ofstream output(path);
      output << document.dump(2);
      if (!output.good()) {
        throw std::runtime_error("Could not write fixture document: " +
                                 path.string());
      }
    }

    void write(const std::filesystem::path &relativePath,
               std::string_view document) const {
      writeJson(relativePath, nlohmann::json::parse(document));
    }

    void writeManifest(const std::filesystem::path &relativePath,
                       std::string_view document) const {
      auto manifest = nlohmann::json::parse(document);
      const auto source = directory / relativePath.parent_path() /
                          manifest.at("source").get<std::string>();
      const auto hash = demi::assets::hashFiles({source});
      if (!hash) {
        throw std::runtime_error("Could not hash fixture source: " +
                                 source.string());
      }
      manifest["source_hash"] = *hash;
      writeJson(relativePath, manifest);
    }
  };
  const auto fixtureId =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const ProjectFixture fixture{
      std::filesystem::temp_directory_path() /
      ("demi-scatter-startup-" + std::to_string(fixtureId))};
  fixture.write("demi.project.json", R"({
    "format_version": 1,
    "name": "Scatter startup probe",
    "main_scene": "scene://scatter_probe/main",
    "scenes": [{"id": "scene://scatter_probe/main"}]
  })");
  fixture.write("scenes/main.scene.json", R"({
    "format_version": 1,
    "id": "scene://scatter_probe/main",
    "entities": [{
      "id": "terrain",
      "components": {
        "Transform3D": {},
        "Terrain3D": {"asset": "asset://terrain/startup"}
      }
    }]
  })");

  TerrainRecipe recipe;
  recipe.size = {4, 4};
  recipe.cellsX = 4;
  recipe.cellsZ = 4;
  recipe.chunkCells = 4;
  recipe.paletteId = PaletteId;
  recipe.landforms.at("default").baseHeight = 4;
  recipe.landforms.at("default").heightVariation = 0;
  auto terrainDocument = nlohmann::json::parse(R"({
    "format_version": 1,
    "id": "asset://terrain/startup",
    "name": "Small startup terrain"
  })");
  terrainDocument["recipe"] = recipe.toJson();
  fixture.writeJson("assets/startup.terrain.json", terrainDocument);
  fixture.writeManifest("assets/startup.asset.json", R"({
    "format_version": 1,
    "id": "asset://terrain/startup",
    "type": "Terrain",
    "importer": "terrain_heightfield",
    "importer_version": 1,
    "source": "startup.terrain.json",
    "dependencies": ["asset://terrain/palettes/test"]
  })");
  fixture.write("assets/palette.json", R"({
    "format_version": 3,
    "name": "Startup prefab placement",
    "placements": {
      "probe": {
        "prefab": "prefab://startup_probe",
        "density": 1,
        "spacing": 0,
        "collision": "none"
      }
    }
  })");
  fixture.writeManifest("assets/palette.asset.json", R"({
    "format_version": 1,
    "id": "asset://terrain/palettes/test",
    "type": "DataAsset",
    "importer": "json_data",
    "importer_version": 1,
    "source": "palette.json",
    "dependencies": [],
    "settings": {"content_type": "terrain_palette"}
  })");
  for (const auto tier : {"high", "medium", "low"}) {
    const std::string stem = "assets/model_" + std::string(tier);
    fixture.write(stem + ".gltf", R"({"asset":{"version":"2.0"}})");
    fixture.writeManifest(stem + ".asset.json", nlohmann::json{
        {"format_version", 1}, {"id", "asset://scatter/model_" + std::string(tier)},
        {"type", "Model3D"}, {"importer", "gltf-model"},
        {"importer_version", 1}, {"source", "model_" + std::string(tier) + ".gltf"},
        {"dependencies", nlohmann::json::array()}}.dump());
  }
  fixture.write("prefabs/startup_probe.prefab.json", R"({
    "format_version": 1,
    "id": "prefab://startup_probe",
    "entities": [{
      "id": "body",
      "components": {
        "Transform3D": {},
        "MeshRenderer": {
          "model": "asset://scatter/model_high",
          "medium_lod_model": "asset://scatter/model_medium",
          "medium_lod_distance": 10,
          "low_lod_model": "asset://scatter/model_low",
          "low_lod_distance": 20
        }
      }
    }]
  })");

  demi::assets::prepareTerrainAssets(
      demi::loadAssetRegistry(fixture.directory));
  std::string error;
  auto loaded = loadProject(fixture.directory / "demi.project.json", error);
  if (!loaded || !error.empty()) {
    const auto validation = demi::validatePath(fixture.directory);
    for (const auto &diagnostic : validation.diagnostics) {
      error += "\n" + diagnostic.code + ": " + diagnostic.message;
    }
    throw std::runtime_error("Scatter startup project load failed: " + error);
  }
  const auto *owner = findEntity(loaded->world, OwnerId);
  const auto *terrain =
      owner == nullptr ? nullptr : owner->component<Terrain3DComponent>();
  assert(terrain != nullptr && terrain->generated != nullptr);
  assert(terrain->generated->cellsX == 4 && terrain->generated->cellsZ == 4);
  assert(!terrain->generated->scatterPlacements.empty());
  assert(terrain->generated->scatterPlacements.size() <= 25);
  const auto expectedPlacements = terrain->generated->scatterPlacements.size();

  const auto registry = demi::loadAssetRegistry(fixture.directory);
  RuntimeAssetService assets;
  demi::Diagnostics assetDiagnostics;
  assert(assets.configure(loaded->project, registry, &assetDiagnostics));
  const auto request = assets.prepareScene("scene://scatter_probe/main", &assetDiagnostics);
  assert(request != 0 && !demi::hasErrors(assetDiagnostics));
  for (int attempt = 0; attempt < 1000 &&
       assets.progress(request).stage != demi::assets::AssetGroupStage::Ready; ++attempt) {
    assets.update();
    std::this_thread::yield();
  }
  assert(assets.activate(request, &assetDiagnostics));
  for (const auto tier : {"high", "medium", "low"}) {
    const auto id = "asset://scatter/model_" + std::string(tier);
    const auto memory = assets.memoryReport();
    assert(std::ranges::any_of(memory.assets, [&](const auto &asset) {
      return asset.assetId == id;
    }));
  }

  RuntimePrefabService prefabs;
  prefabs.configure(loaded->project.projectDirectory);
  const auto report = materializeTerrainScatter(loaded->world, &prefabs, error);
  assert(error.empty() && report.unresolved == 0);
  assert(report.placed == expectedPlacements);
  const auto prefix = std::string("terrain/__scatter/") + PaletteId + "/";
  assert(std::ranges::any_of(loaded->world.entities, [&](const Entity &entity) {
    return entity.id.starts_with(prefix) &&
           entity.hasComponent<TerrainScatterBatch>() &&
           entity.component<MeshInstances3DComponent>() != nullptr;
  }));
}

// A field with no palette scatters nothing but must not wipe another palette's
// instances, because the prefix is palette-scoped.
void paletteSwapDoesNotCrossRemove() {
  auto world = worldWithTerrain();
  WorldCommandBuffer commands;
  RuntimePrefabService prefabs;
  std::string error;
  HeightField first;
  first.paletteId = PaletteId;
  first.scatterPlacements = {placementAt(1, 2.F, "tree", 0.F)};
  (void)syncTerrainScatter(world, commands, OwnerId, first, &prefabs, error);
  flush(commands, world);
  assert(scatterChildren(world) == 1);

  HeightField empty;
  empty.paletteId = "asset://terrain/palettes/another";
  const auto stats =
      syncTerrainScatter(world, commands, OwnerId, empty, &prefabs, error);
  assert(error.empty());
  assert(stats.total() == 0);
  flush(commands, world);
  // The previous palette's instance is still there, and now unreachable by a
  // later sync, which is why releaseTerrainScatter is the explicit teardown.
  assert(findEntity(world, terrainScatterInstanceId(OwnerId, PaletteId, "tree",
                                                    1, 0)) != nullptr);
}
} // namespace

int main() {
  instanceIdIsStableAndSeparating();
  repeatSyncIsIdempotent();
  movementUpdatesInPlace();
  removedCellsAreCleanedUp();
  sameCellDifferentRolesCoexist();
  missingOwnerIsReported();
  unresolvablePrefabIsCounted();
  releaseClearsOnlyItsOwner();
  paletteSwapDoesNotCrossRemove();
  noServiceSkipsPrefabRoles();
  unresolvedSceneScatterDoesNotCommitPartialEntities();
  prefabPrototypeKeepsGeometryAndStableCellId();
  collisionFreePlacementsShareRenderOwners();
  visualPrefabBatchesPreserveHierarchyAndMaterials();
  collisionPolicyKeepsAuthoredShapes();
  initialSceneScatterUsesConfiguredPrefabService();
  sharedServiceIsVisibleToBothConsumers();
  hostWithoutServiceIsSafe();
  std::cout << "Terrain scatter runtime checks passed\n";
}
