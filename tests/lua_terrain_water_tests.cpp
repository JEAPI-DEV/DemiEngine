#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/components/gameplay/LuaScriptComponent.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainWater.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace demi::runtime;
namespace {
void run(LuaScriptHost &host, const char *script) {
  const auto result = host.executeConsole(script);
  if (!result.succeeded)
    throw std::runtime_error(result.error);
}
void tests() {
  TerrainRecipe recipe;
  recipe.size = {8, 8};
  recipe.cellsX = recipe.cellsZ = 8;
  recipe.landforms.at("default").heightVariation = 0;
  recipe.landforms.at("default").baseHeight = 0;
  auto field = TerrainGenerator::generate(recipe);
  TerrainWaterAuthoring water;
  water.authored = true;
  water.bodies.push_back({.id = "lake",
                          .kind = TerrainWaterBody::Lake,
                          .level = 2,
                          .center = {4, 4}});
  auto result = carveTerrainWater(*field, water, nullptr);
  field->heights = result->carvedHeights;
  auto artifacts = std::make_shared<TerrainGraphArtifacts>();
  artifacts->water = water;
  artifacts->waterResult = std::move(*result);
  field->graphArtifacts = std::move(artifacts);
  World world;
  world.activeSceneId = "scene://water";
  Entity owner;
  owner.id = "terrain";
  owner.setComponent(
      Transform3DComponent{.position = {10, 5, 20}, .scale = {2, 3, 2}});
  Terrain3DComponent terrain;
  terrain.generated = std::make_shared<const HeightField>(std::move(*field));
  owner.setComponent(std::move(terrain));
  world.entities.push_back(std::move(owner));
  Entity observer;
  observer.id = "observer";
  observer.setComponent(
      Transform3DComponent{.parent = "terrain", .position = {2, 1, 2}});
  observer.setComponent(LuaScriptComponent{
      .module = "script://scripts/water_sensor.lua", .propertiesJson = "{}"});
  world.entities.push_back(std::move(observer));
  world.terrainOwners.push_back({.id = "terrain"});
  InputState input;
  LuaScriptHost host;
  std::string error;
  if (!host.initialize(world, input, nullptr, error))
    throw std::runtime_error(error);
  run(host, R"lua(
    local Water = require("demi.terrain.water")
    local Transform = require("demi.transform3d")
    local x,y,z = Transform.get_world_position('observer')
    assert(x == 14 and y == 8 and z == 24)
    assert(Transform.get_world_position('missing') == nil)
    assert(TerrainWater == nil and WaterTracker == nil)
    local sample = Water.sample({14,8,24})
    assert(sample.terrain_id == 'terrain' and sample.body_id == 'lake' and sample.kind == 'lake')
    assert(sample.underwater and math.abs(sample.surface[2]-11) < 0.001)
    assert(math.abs(sample.depth-7.5) < 0.001 and #sample.normal == 3)
    assert(Water.sample({14,100,24}).underwater == false)
    assert(Water.sample({0,0,0}) == nil)
    assert(Water.sample({14,8,24}, 'missing') == nil)
    for _, position in ipairs({{1,2}, {1,2,3,4}, {1,'x',3}, {1,math.huge,3}}) do
      assert(not pcall(Water.sample, position))
    end
    water_tracker = Water.tracker()
    assert(#water_tracker:update({14,100,24}) == 0)
    local events = water_tracker:update({14,8,24})
    assert(#events == 1 and events[1].phase == 'enter')
    assert(water_tracker:current().underwater)
    assert(water_tracker:update({14,8,24})[1].phase == 'stay')
    assert(water_tracker:update({14,100,24})[1].phase == 'exit')
    assert(water_tracker:current() == nil and #water_tracker:update({14,100,24}) == 0)
    water_tracker:update({14,8,24})
    water_tracker:reset()
    assert(water_tracker:current() == nil)
    water_tracker:update({14,8,24})
  )lua");
  ProjectData project;
  project.projectDirectory =
      std::filesystem::path(__FILE__).parent_path().parent_path() /
      "examples/terrain_graph_3d";
  project.name = "Water sensor integration";
  if (!host.loadWorldScripts(project, world, error))
    throw std::runtime_error(error);
  host.start();
  run(host, R"lua(
    sensor_events = {}
    local Events = require('demi.events')
    for _, phase in ipairs({'enter', 'exit', 'stay'}) do
      Events.subscribe('water_' .. phase, function(value)
        assert(value.entity_id == 'observer' and value.water.body_id == 'lake')
        sensor_events[#sensor_events+1] = phase
      end)
    end
  )lua");
  host.fixedUpdate(1.F / 60);
  host.fixedUpdate(1.F / 60);
  run(host,
      "assert(sensor_events[1] == 'enter' and sensor_events[2] == 'stay')");
  findEntity(world, "observer")->component<Transform3DComponent>()->position.y =
      100;
  host.fixedUpdate(1.F / 60);
  run(host, "assert(sensor_events[3] == 'exit' and #sensor_events == 3)");
  world.entities.front().enabled = false;
  run(host,
      R"lua(assert(water_tracker:update({14,8,24})[1].phase == 'exit'))lua");
  world.entities.clear();
  world.terrainOwners.clear();
  run(host, R"lua(
    assert(require('demi.terrain.water').sample({14,8,24}) == nil)
    assert(#water_tracker:update({14,8,24}) == 0)
  )lua");
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
