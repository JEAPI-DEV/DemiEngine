#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainWaterRuntime.h"
#include "demi/runtime/terrain/TerrainWaterTracker.h"
#include "demi/runtime/terrain/TerrainWorld.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace demi::runtime;
namespace {
void check(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
std::shared_ptr<const HeightField> makeField(float level = 2) {
  TerrainRecipe recipe;
  recipe.size = {8, 8};
  recipe.cellsX = recipe.cellsZ = 8;
  recipe.chunkCells = 4;
  recipe.landforms.at("default").baseHeight = 0;
  recipe.landforms.at("default").heightVariation = 0;
  recipe.graph = defaultTerrainGraph();
  recipe.graph["nodes"].push_back({{"id", "lake"},
                                   {"type", "water"},
                                   {"parameters",
                                    {{"kind", "lake"},
                                     {"level", level},
                                     {"center_x", 4},
                                     {"center_z", 4},
                                     {"radius", 0}}}});
  recipe.graph["links"][0]["from"]["node"] = "lake";
  recipe.graph["links"].push_back(
      {{"id", "source_lake"},
       {"from", {{"node", "landform"}, {"port", "field"}}},
       {"to", {{"node", "lake"}, {"port", "field"}}}});
  recipe.graph["links"].push_back(
      {{"id", "lake_water"},
       {"from", {{"node", "lake"}, {"port", "water"}}},
       {"to", {{"node", "terrain_output"}, {"port", "water"}}}});
  auto field = executeTerrainGraph(recipe);
  check(bool(field), "Could not generate query fixture");
  return std::make_shared<const HeightField>(std::move(*field));
}
void addOwner(World &world, std::string id, Vec3 position,
              std::shared_ptr<const HeightField> field) {
  Entity owner;
  owner.id = id;
  owner.sceneOwner = "scene://water";
  owner.setComponent(
      Transform3DComponent{.position = position, .scale = {2, 3, 2}});
  Terrain3DComponent terrain;
  terrain.generated = std::move(field);
  owner.setComponent(std::move(terrain));
  world.entities.push_back(std::move(owner));
  world.terrainOwners.push_back({.id = std::move(id)});
}
void tests() {
  auto field = makeField();
  World world;
  world.activeSceneId = "scene://water";
  addOwner(world, "first", {10, 5, 20}, field);
  addOwner(world, "second", {100, 5, 20}, field);
  TerrainWaterRuntime runtime;
  auto sample = runtime.sample(world, {14, 8, 24});
  check(sample && sample->terrainId == "first" && sample->bodyId == "lake" &&
            sample->underwater,
        "Translated/scaled water query failed");
  check(std::abs(sample->surface.y - 11) < 1e-4F &&
            std::abs(sample->depth - 7.5F) < 1e-4F,
        "Water measurements are not in world units");
  check(runtime.cachedFieldCount() == 1, "Unexpected context count");
  check(runtime.sample(world, {104, 8, 24}, "second")->terrainId == "second" &&
            runtime.cachedFieldCount() == 1,
        "Shared placements copied query contexts");
  check(!runtime.sample(world, {14, 8, 24}, "second"),
        "Terrain filter ignored");
  check(!runtime.sample(world, {0, 0, 0}), "Dry position reported water");
  check(runtime.sample(world, {14, 100, 24}) &&
            !runtime.sample(world, {14, 100, 24})->underwater,
        "Air point reported immersion");

  TerrainWaterTracker tracker;
  auto events = tracker.update(sample);
  check(events.size() == 1 && events[0].phase == "enter",
        "Initial enter missing");
  events = tracker.update(runtime.sample(world, {14, 8, 24}));
  check(events.size() == 1 && events[0].phase == "stay", "Stay missing");
  events = tracker.update(runtime.sample(world, {104, 8, 24}));
  check(events.size() == 2 && events[0].phase == "exit" &&
            events[1].phase == "enter",
        "Body switch must exit then enter");
  world.entities[1].enabled = false;
  events = tracker.update(runtime.sample(world, {104, 8, 24}));
  check(events.size() == 1 && events[0].phase == "exit",
        "Disabled owner did not exit");
  check(tracker.update(std::nullopt).empty(), "Repeated exit emitted");

  auto *owner = findEntity(world, "first");
  owner->component<Transform3DComponent>()->rotation = {.5F, .8F, .2F};
  const auto transform = *resolveWorldTransform3D(world, *owner);
  const auto worldPoint = transformPoint3D(transform, {2, 1, 2});
  const auto rotated = runtime.sample(world, worldPoint, "first");
  check(rotated && rotated->underwater &&
            std::abs(std::hypot(rotated->normal.x, rotated->normal.y,
                                rotated->normal.z) -
                     1) < 1e-4F,
        "Rotated terrain query failed");
  const auto expected = transformPoint3D(transform, {2, 2, 2});
  check(std::hypot(rotated->surface.x - expected.x,
                   rotated->surface.y - expected.y,
                   rotated->surface.z - expected.z) < 1e-4F,
        "Rotated surface point incorrect");
  owner->component<Transform3DComponent>()->position = {};
  owner->component<Transform3DComponent>()->rotation = {};
  owner->component<Transform3DComponent>()->scale = {1e-7F, 1e-7F, 1e-7F};
  const auto tiny = runtime.sample(world, {2e-7F, 1e-7F, 2e-7F}, "first");
  check(tiny && tiny->underwater && std::abs(tiny->surface.x - 2e-7F) < 1e-10F,
        "Nonzero tiny scales were collapsed by inverse transform");
  owner->component<Transform3DComponent>()->position = transform.position;
  owner->component<Transform3DComponent>()->rotation = transform.rotation;
  owner->component<Transform3DComponent>()->scale = transform.scale;
  tracker.update(rotated);
  owner->component<Terrain3DComponent>()->generated = makeField(0);
  events = tracker.update(runtime.sample(world, worldPoint, "first"));
  check(events.size() == 1 && events[0].phase == "exit",
        "Regenerated dry water did not exit");
  world.entities.clear();
  world.terrainOwners.clear();
  check(!runtime.sample(world, worldPoint) && runtime.cachedFieldCount() == 0,
        "Unloaded world retained query data");
  tracker.reset();
  check(!tracker.current(), "Reset did not clear tracking");
  auto identity = *sample;
  identity.underwater = true;
  tracker.update(identity);
  identity.sceneId = "scene://other";
  const auto changedScene = tracker.update(identity);
  check(changedScene.size() == 2 && changedScene[0].phase == "exit" &&
            changedScene[1].phase == "enter",
        "Scene identity was ignored by tracker");
}
} // namespace
int main() {
  try {
    tests();
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
