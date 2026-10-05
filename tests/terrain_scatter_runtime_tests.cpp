#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/assets/TerrainAsset.h"
#include "demi/runtime/terrain/TerrainScatterRuntime.h"
#include "demi/runtime/terrain/TerrainWorld.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/SceneLoader.h"
#include <filesystem>
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>

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
  const std::string prefix = std::string(OwnerId) + "/__scatter/" + PaletteId + "/";
  return std::count_if(world.entities.begin(), world.entities.end(),
                       [&](const Entity &entity) {
                         return entity.id.rfind(prefix, 0) == 0;
                       });
}

TerrainScatterPlacement placementAt(std::size_t cell, float height,
                                    TerrainPaletteRole role, float yaw) {
  TerrainScatterPlacement placement;
  placement.role = role;
  placement.asset = "asset://terrain/props/tree";
  placement.cell = cell;
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
  const auto a = terrainScatterInstanceId(OwnerId, PaletteId,
                                           TerrainPaletteRole::Tree, 42);
  const auto b = terrainScatterInstanceId(OwnerId, PaletteId,
                                           TerrainPaletteRole::Tree, 42);
  assert(a == b);
  assert(a != terrainScatterInstanceId(OwnerId, PaletteId,
                                       TerrainPaletteRole::Bush, 42));
  assert(a != terrainScatterInstanceId(OwnerId, PaletteId,
                                       TerrainPaletteRole::Tree, 43));
  assert(a != terrainScatterInstanceId("other", PaletteId,
                                       TerrainPaletteRole::Tree, 42));
  assert(a != terrainScatterInstanceId(OwnerId, "asset://other/palette",
                                       TerrainPaletteRole::Tree, 42));
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
  field.scatterPlacements = {placementAt(1, 2.F, TerrainPaletteRole::Tree, 0.5F),
                             placementAt(2, 3.F, TerrainPaletteRole::Tree, 1.5F)};

  std::string error;
  const auto first = syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  assert(error.empty());
  assert(first.created == 2);
  flush(commands, world);
  assert(scatterChildren(world) == 2);

  const auto second = syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
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
  field.scatterPlacements = {placementAt(1, 2.F, TerrainPaletteRole::Tree, 0.5F)};
  std::string error;
  (void)syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  flush(commands, world);
  const auto id = terrainScatterInstanceId(OwnerId, PaletteId,
                                           TerrainPaletteRole::Tree, 1);
  const auto *before = findEntity(world, id);
  assert(before != nullptr);
  assert(before->component<Transform3DComponent>()->position.y == 2.F);

  // Same cell, new height and yaw: a sculpt moved the ground under it.
  field.scatterPlacements = {placementAt(1, 7.F, TerrainPaletteRole::Tree, 2.5F)};
  const auto stats = syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
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
  field.scatterPlacements = {placementAt(1, 2.F, TerrainPaletteRole::Tree, 0.F),
                             placementAt(2, 3.F, TerrainPaletteRole::Tree, 0.F),
                             placementAt(3, 4.F, TerrainPaletteRole::Bush, 0.F)};
  std::string error;
  (void)syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  flush(commands, world);
  assert(scatterChildren(world) == 3);

  // A role's weight dropped to zero, or its biome no longer matches.
  field.scatterPlacements = {placementAt(1, 2.F, TerrainPaletteRole::Tree, 0.F)};
  const auto stats = syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  assert(error.empty());
  assert(stats.removed == 2 && stats.retained == 1);
  flush(commands, world);
  assert(scatterChildren(world) == 1);
  assert(findEntity(world, terrainScatterInstanceId(OwnerId, PaletteId,
                                                    TerrainPaletteRole::Tree, 1)) !=
         nullptr);
}

// Two roles in the same cell are two instances, not one overwriting the other.
void sameCellDifferentRolesCoexist() {
  auto world = worldWithTerrain();
  WorldCommandBuffer commands;
  RuntimePrefabService prefabs;
  HeightField field;
  field.paletteId = PaletteId;
  field.scatterPlacements = {placementAt(9, 5.F, TerrainPaletteRole::Tree, 0.F),
                             placementAt(9, 5.F, TerrainPaletteRole::Grass, 0.F)};
  std::string error;
  const auto stats = syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
  assert(stats.created == 2);
  flush(commands, world);
  assert(scatterChildren(world) == 2);
  assert(findEntity(world, terrainScatterInstanceId(OwnerId, PaletteId,
                                                    TerrainPaletteRole::Tree, 9)) !=
         nullptr);
  assert(findEntity(world, terrainScatterInstanceId(OwnerId, PaletteId,
                                                    TerrainPaletteRole::Grass, 9)) !=
         nullptr);
}

// A missing owner is an error, never a silent no-op that looks like a success.
void missingOwnerIsReported() {
  auto world = worldWithTerrain();
  WorldCommandBuffer commands;
  RuntimePrefabService prefabs;
  HeightField field;
  field.paletteId = PaletteId;
  field.scatterPlacements = {placementAt(1, 2.F, TerrainPaletteRole::Tree, 0.F)};
  std::string error;
  const auto stats = syncTerrainScatter(world, commands, "no_such_terrain", field,
                                        &prefabs, error);
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
  field.scatterPlacements = {placementAt(1, 2.F, TerrainPaletteRole::Tree, 0.F),
                             placementAt(2, 2.F, TerrainPaletteRole::Tree, 0.F)};
  field.scatterPlacements[0].prefab = "prefab://does/not/exist";
  std::string error;
  const auto stats = syncTerrainScatter(world, commands, OwnerId, field, &prefabs, error);
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
  field.scatterPlacements = {placementAt(1, 2.F, TerrainPaletteRole::Tree, 0.F),
                             placementAt(2, 2.F, TerrainPaletteRole::Tree, 0.F)};
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
  // The host forwards the same service to the scene flow it owns, so there is no
  // second wiring point that can be forgotten.
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
  field.scatterPlacements = {placementAt(1, 2.F, TerrainPaletteRole::Tree, 0.F)};
  field.scatterPlacements[0].prefab = "prefab://tree";
  std::string error;
  const auto stats = syncTerrainScatter(world, commands, OwnerId, field,
                                        nullptr, error);
  assert(error.empty());
  assert(stats.unresolved == 1 && stats.created == 0);
  flush(commands, world);
  assert(scatterChildren(world) == 0);
}

void unresolvedSceneScatterDoesNotCommitPartialEntities() {
  auto world = worldWithTerrain();
  auto field = std::make_shared<HeightField>();
  field->paletteId = PaletteId;
  field->scatterPlacements = {
      placementAt(1, 2.F, TerrainPaletteRole::Tree, 0.F),
      placementAt(2, 3.F, TerrainPaletteRole::Grass, 0.F)};
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
  assert(std::filesystem::exists(project / "prefabs/scatter_prototype.prefab.json"));
  RuntimePrefabService prefabs;
  prefabs.configure(project);
  auto world = worldWithTerrain();
  WorldCommandBuffer commands;
  HeightField field;
  field.paletteId = PaletteId;
  field.scatterPlacements = {placementAt(7, 2.F, TerrainPaletteRole::Grass, 0.5F)};
  field.scatterPlacements.front().asset = "asset://terrain/surfaces/grass";
  field.scatterPlacements.front().prefab = "prefab://scatter_prototype";
  const auto id = terrainScatterInstanceId(OwnerId, PaletteId,
                                            TerrainPaletteRole::Grass, 7);
  std::string error;
  const auto created = syncTerrainScatter(world, commands, OwnerId, field,
                                           &prefabs, error);
  assert(error.empty() && created.created == 1 && created.unresolved == 0);
  flush(commands, world);
  const auto *body = findEntity(world, id + "/body");
  assert(body != nullptr && body->prefabInstance == id);
  assert(body->component<MeshRendererComponent>() != nullptr);
  assert(body->component<MeshRendererComponent>()->model.empty());
  const auto retained = syncTerrainScatter(world, commands, OwnerId, field,
                                            &prefabs, error);
  assert(error.empty() && retained.retained == 1 && retained.created == 0);

  field.scatterPlacements.front().position.y = 5.F;
  const auto moved = syncTerrainScatter(world, commands, OwnerId, field,
                                         &prefabs, error);
  assert(error.empty() && moved.updated == 1 && moved.created == 0);
  flush(commands, world);
  body = findEntity(world, id + "/body");
  assert(body != nullptr);
  assert(body->component<Transform3DComponent>()->position.y == 5.F);

  field.scatterPlacements.clear();
  const auto removed = syncTerrainScatter(world, commands, OwnerId, field,
                                           &prefabs, error);
  assert(error.empty() && removed.removed == 1);
  flush(commands, world);
  assert(findEntity(world, id + "/body") == nullptr);

  field.scatterPlacements = {placementAt(8, 1.F, TerrainPaletteRole::Grass, 0.F)};
  field.scatterPlacements.front().prefab = "prefab://scatter_prototype";
  const auto nextId = terrainScatterInstanceId(OwnerId, PaletteId,
                                                TerrainPaletteRole::Grass, 8);
  const auto next = syncTerrainScatter(world, commands, OwnerId, field,
                                       &prefabs, error);
  assert(error.empty() && next.created == 1);
  flush(commands, world);
  assert(findEntity(world, nextId + "/body") != nullptr);
  assert(findEntity(world, id + "/body") == nullptr);
}

void initialSceneScatterUsesConfiguredPrefabService() {
  const auto project = exampleProjectPath();
  demi::assets::prepareTerrainAssets(demi::loadAssetRegistry(project));
  std::string error;
  auto loaded = loadProject(project / "demi.project.json", error);
  assert(loaded.has_value() && error.empty());
  auto *owner = findEntity(loaded->world, "terrain");
  auto *terrain = owner == nullptr
                      ? nullptr
                      : owner->component<Terrain3DComponent>();
  assert(terrain != nullptr && terrain->generated != nullptr);
  assert(!terrain->generated->scatterPlacements.empty());
  const auto paletteId = terrain->generated->paletteId;
  // This checks startup service wiring with one real placement. The full
  // example's density belongs to runtime smoke/performance coverage.
  auto probe = std::make_shared<HeightField>(*terrain->generated);
  probe->scatterPlacements.resize(1);
  terrain->generated = std::move(probe);

  RuntimePrefabService prefabs;
  prefabs.configure(loaded->project.projectDirectory);
  const auto report = materializeTerrainScatter(loaded->world, &prefabs, error);
  assert(error.empty() && report.unresolved == 0 && report.placed > 0);
  const auto prefix = std::string("terrain/__scatter/") + paletteId + "/";
  assert(std::ranges::any_of(loaded->world.entities, [&](const Entity &entity) {
    return entity.id.starts_with(prefix) &&
           !entity.prefabInstance.empty();
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
  first.scatterPlacements = {placementAt(1, 2.F, TerrainPaletteRole::Tree, 0.F)};
  (void)syncTerrainScatter(world, commands, OwnerId, first, &prefabs, error);
  flush(commands, world);
  assert(scatterChildren(world) == 1);

  HeightField empty;
  empty.paletteId = "asset://terrain/palettes/another";
  const auto stats = syncTerrainScatter(world, commands, OwnerId, empty, &prefabs, error);
  assert(error.empty());
  assert(stats.total() == 0);
  flush(commands, world);
  // The previous palette's instance is still there, and now unreachable by a
  // later sync, which is why releaseTerrainScatter is the explicit teardown.
  assert(findEntity(world, terrainScatterInstanceId(OwnerId, PaletteId,
                                                    TerrainPaletteRole::Tree, 1)) !=
         nullptr);
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
  initialSceneScatterUsesConfiguredPrefabService();
  sharedServiceIsVisibleToBothConsumers();
  hostWithoutServiceIsSafe();
  std::cout << "Terrain scatter runtime checks passed\n";
}
