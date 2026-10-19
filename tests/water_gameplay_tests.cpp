#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scene/model/ProjectData.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainWater.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace demi::runtime;
namespace {

class Fixture {
public:
  World world;
  InputState input;
  ProjectData project;
  LuaScriptHost host;
  std::filesystem::path directory;

  explicit Fixture(Vec3 position = {2, 1, 4}, Vec3 velocity = {},
                   float flowSpeed = 0) {
    directory =
        std::filesystem::temp_directory_path() /
        ("demi-water-gameplay-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory / "scripts");
    std::filesystem::copy(std::filesystem::path(DEMI_SOURCE_DIR) /
                              "packages/sources/demi.gameplay.water/scripts",
                          directory / "scripts",
                          std::filesystem::copy_options::recursive);
    TerrainRecipe recipe;
    recipe.size = {16, 16};
    recipe.cellsX = recipe.cellsZ = 16;
    recipe.landforms.at("default").heightVariation = 0;
    recipe.landforms.at("default").baseHeight = 0;
    auto field = TerrainGenerator::generate(recipe);
    assert(field);
    TerrainWaterAuthoring water;
    water.authored = true;
    water.bodies.push_back({.id = "lake",
                            .kind = TerrainWaterBody::Lake,
                            .level = 2,
                            .flowSpeed = flowSpeed});
    auto carved = carveTerrainWater(*field, water, nullptr);
    assert(carved);
    field->heights = carved->carvedHeights;
    auto artifacts = std::make_shared<TerrainGraphArtifacts>();
    artifacts->water = water;
    artifacts->waterResult = std::move(*carved);
    field->graphArtifacts = std::move(artifacts);
    Entity terrain;
    terrain.id = "terrain";
    terrain.setComponent(Transform3DComponent{});
    Terrain3DComponent generated;
    generated.generated =
        std::make_shared<const HeightField>(std::move(*field));
    terrain.setComponent(std::move(generated));
    world.entities.push_back(std::move(terrain));
    world.terrainOwners.push_back({.id = "terrain"});
    Entity body;
    body.id = "body";
    body.setComponent(Transform3DComponent{.position = position});
    body.setComponent(SphereCollider3DComponent{.radius = .1F});
    Rigidbody3DComponent rigidbody;
    rigidbody.bodyType = "dynamic";
    rigidbody.useGravity = false;
    rigidbody.mass = 3;
    rigidbody.velocity = velocity;
    rigidbody.linearDamping = 0;
    rigidbody.allowSleep = false;
    body.setComponent(std::move(rigidbody));
    world.entities.push_back(std::move(body));
    world.activeSceneId = "scene://water_gameplay";
    ensurePhysicsWorld3D(world).step(world, 1.F / 120, {});
    project.projectDirectory = directory;
    std::string error;
    if (!host.initialize(world, input, nullptr, error) ||
        !host.loadWorldScripts(project, world, error)) {
      throw std::runtime_error(error);
    }
  }

  ~Fixture() {
    host.destroy();
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
  }

  void run(const std::string &source) {
    const auto result = host.executeConsole(source);
    if (!result.succeeded) {
      throw std::runtime_error(result.error);
    }
  }

  void step(float dt) { ensurePhysicsWorld3D(world).step(world, dt, {}); }
  Vec3 velocity() const {
    const auto result = world.physicsWorld3D->velocity("body");
    assert(result);
    return *result;
  }
};

float dragResponse(float dt, int count) {
  Fixture fixture({2, 1, 4}, {4, 0, 0});
  fixture.run(R"lua(
    local Buoyancy = require("demi.gameplay.water.buoyancy")
    probe = Buoyancy.new("body", {half_height=.1, density_ratio=0, linear_drag=2})
  )lua");
  for (int step = 0; step < count; ++step) {
    fixture.run("assert(probe:fixed_update(" + std::to_string(dt) +
                ").active)");
    fixture.step(dt);
  }
  return fixture.velocity().x;
}

void actualPhysicsDragComposesAcrossFixedSteps() {
  const float coarse = dragResponse(.1F, 10);
  const float fine = dragResponse(.05F, 20);
  assert(std::abs(coarse - fine) < .002F);
  assert(std::abs(coarse - 4 * std::exp(-2.F)) < .002F);
}

void realFlowDrivesDragAndSwimmingEligibility() {
  Fixture drifting({2, 1, 4}, {}, 2);
  drifting.run(R"lua(
    local Buoyancy = require("demi.gameplay.water.buoyancy")
    local Swimming = require("demi.gameplay.water.swimming")
    local probe = Buoyancy.new("body", {half_height=.1, density_ratio=0, linear_drag=2})
    assert(probe:fixed_update(.1).active)
    assert(not Swimming.new("body", {max_flow=1}):fixed_update(.1, {1,0,0}).active)
  )lua");
  drifting.step(.1F);
  const float expectedVelocity = 2 * (1 - std::exp(-.2F));
  assert(std::abs(drifting.velocity().x - expectedVelocity) < .002F);
  assert(std::abs(drifting.velocity().y) < .002F);

  Fixture swimming({2, 1, 4}, {2, 0, 0}, 2);
  swimming.run(R"lua(
    local Swimming = require("demi.gameplay.water.swimming")
    local probe = Swimming.new("body", {max_flow=2, linear_drag=2})
    assert(probe:fixed_update(.1, {0,0,0}).active)
  )lua");
  swimming.step(.1F);
  assert(std::abs(swimming.velocity().x - 2) < .002F);
}

void realBuoyancyAndSwimmingAddForcesWithoutAirMovement() {
  Fixture wet;
  wet.run(R"lua(
    local Buoyancy = require("demi.gameplay.water.buoyancy")
    local Swimming = require("demi.gameplay.water.swimming")
    float = Buoyancy.new("body", {half_height=.1, linear_drag=0})
    swim = Swimming.new("body", {linear_drag=0, min_depth=1, speed=3})
    assert(float:fixed_update(.1).active)
    assert(swim:fixed_update(.1, {1,0,0}).active)
  )lua");
  wet.step(.1F);
  assert(wet.velocity().y > .9F);
  assert(wet.velocity().x > .7F && wet.velocity().x <= .81F);
  Fixture air({2, 5, 4});
  air.run(R"lua(
    local Buoyancy = require("demi.gameplay.water.buoyancy")
    local Swimming = require("demi.gameplay.water.swimming")
    assert(not Buoyancy.new("body"):fixed_update(.1).active)
    assert(not Swimming.new("body"):fixed_update(.1, {1,0,0}).active)
  )lua");
  air.step(.1F);
  assert(air.velocity().x == 0 && air.velocity().y == 0);
  wet.run(R"lua(
    local Rigidbody = require("demi.physics.rigidbody3d")
    local Swimming = require("demi.gameplay.water.swimming")
    local Buoyancy = require("demi.gameplay.water.buoyancy")
    assert(not Swimming.new("body", {min_depth=100}):fixed_update(.1, {1,0,0}).active)
    assert(not pcall(function() Buoyancy.new("body", {half_height=0}) end))
    assert(not pcall(function() Swimming.new("body", {speed=false}) end))
    assert(not pcall(function() float:fixed_update(0) end))
    assert(Rigidbody.set_enabled("body", false))
    assert(not float:fixed_update(.1).active)
    assert(not swim:fixed_update(.1, {1,0,0}).active)
    assert(Rigidbody.set_enabled("body", true))
    local Entity = require("demi.entity")
    assert(Entity.set_enabled("terrain", false))
  )lua");
  // Entity mutations become visible at the host lifecycle command boundary.
  wet.host.update(0);
  wet.run(R"lua(
    assert(not float:fixed_update(.1).active)
    assert(not swim:fixed_update(.1, {1,0,0}).active)
  )lua");
}

} // namespace

int main() {
  PhysicsWorld3D physics;
  if (!physics.available()) {
    std::cout
        << "Water gameplay native physics probe unavailable on this build\n";
    return 0;
  }
  actualPhysicsDragComposesAcrossFixedSteps();
  realFlowDrivesDragAndSwimmingEligibility();
  realBuoyancyAndSwimmingAddForcesWithoutAirMovement();
  std::cout << "Water gameplay actual Lua/physics probes passed\n";
}
