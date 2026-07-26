#include "demi/runtime/scene/ResourceLifetimeRegistry.h"
#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scene/RuntimePrefabService.h"
#include "demi/runtime/scene/SceneFlow.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scripting/LuaScriptHost.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace demi::runtime;

namespace {

bool waitUntilSettled(SceneFlow &flow) {
  for (int attempt = 0; attempt < 1000; ++attempt) {
    flow.poll();
    if (flow.state() != ScenePreparationState::Loading)
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return false;
}

bool writeFile(const std::filesystem::path &path,
               const std::string_view contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << contents;
  return output.good();
}

Entity assetEntity(const std::string &id, const std::string &owner,
                   const std::string &texture, const std::string &audio = {}) {
  nlohmann::json components = {
      {"Sprite", {{"texture", texture}, {"size", {1.0, 1.0}}}},
  };
  if (!audio.empty())
    components["AudioSource"] = {{"clip", audio}};
  std::string error;
  auto entity = RuntimeObjectModel::buildEntity(
      {{"id", id}, {"components", std::move(components)}}, error);
  if (!entity)
    throw std::runtime_error(error);
  entity->sceneOwner = owner;
  return std::move(*entity);
}

bool testTransactionalResources() {
  std::vector<std::string> acquired;
  std::vector<std::string> released;
  ResourceLifetimeRegistry resources{
      [&](const std::string_view asset, std::string &error) {
        acquired.emplace_back(asset);
        if (asset == "asset://textures/second") {
          error = "injected second-asset load failure";
          return false;
        }
        return true;
      },
      [&](const std::string_view asset) { released.emplace_back(asset); }};
  const std::vector entities{
      assetEntity("ent_assets", "scene://fault", "asset://textures/first",
                  "asset://textures/second")};
  std::string error;
  if (resources.tryCapture("scene://fault", entities, error) ||
      error != "injected second-asset load failure" ||
      resources.groupCount() != 0 || acquired.size() != 2 ||
      released != std::vector<std::string>{"asset://textures/first"}) {
    std::cerr << "Partial resource acquisition was not rolled back.\n";
    return false;
  }

  ResourceLifetimeRegistry shared;
  const std::vector ownerA{
      assetEntity("ent_a", "scene://a", "asset://textures/shared")};
  const std::vector ownerB{
      assetEntity("ent_b", "scene://b", "asset://textures/shared")};
  shared.capture("scene://a", ownerA);
  shared.capture("scene://b", ownerB);
  if (shared.referenceCount("asset://textures/shared") != 2)
    return false;
  shared.release("scene://a");
  if (!shared.isReferenced("asset://textures/shared") ||
      shared.referenceCount("asset://textures/shared") != 1)
    return false;
  shared.release("scene://b");
  if (shared.isReferenced("asset://textures/shared"))
    return false;
  return true;
}

std::optional<World> freshWorld(const ProjectData &project) {
  std::string error;
  return loadScene(project, project.mainScene, error);
}

bool testPreparationFailures(const ProjectData &project) {
  SceneFlow flow;
  flow.configure(project);
  ResourceLifetimeRegistry resources;
  auto loaded = freshWorld(project);
  if (!loaded)
    return false;
  World world = std::move(*loaded);
  const std::size_t entityCount = world.entities.size();
  const std::string activeScene = world.activeSceneId;

  if (!flow.prepare("scene://minimal_2d_networking/platformer", false) ||
      flow.state() != ScenePreparationState::Loading || !flow.cancel() ||
      flow.state() != ScenePreparationState::Cancelled ||
      flow.activate(world, resources).has_value() ||
      world.entities.size() != entityCount ||
      world.activeSceneId != activeScene) {
    std::cerr << "Cancelled preparation mutated the active scene.\n";
    return false;
  }

  if (!flow.prepare("scene://missing/fails_after_lookup", false) ||
      !waitUntilSettled(flow) ||
      flow.state() != ScenePreparationState::Failed ||
      flow.error().empty() || flow.activate(world, resources).has_value() ||
      world.entities.size() != entityCount ||
      world.activeSceneId != activeScene) {
    std::cerr << "Failed preparation did not preserve the active scene.\n";
    return false;
  }
  return true;
}

bool testPersistentReplacement(const ProjectData &project) {
  auto loaded = freshWorld(project);
  if (!loaded)
    return false;
  World world = std::move(*loaded);
  if (world.entities.empty())
    return false;
  const std::string persistentId = world.entities.front().id;
  world.entities.front().persistent = true;
  ResourceLifetimeRegistry resources;
  SceneFlow flow;
  flow.configure(project);
  if (!flow.prepare("scene://minimal_2d_networking/spiral", false) ||
      !waitUntilSettled(flow) || !flow.activate(world, resources) ||
      findEntity(world, persistentId) == nullptr ||
      findEntity(world, persistentId)->sceneOwner != "persistent" ||
      !world.loadedSceneIds.contains(
          "scene://minimal_2d_networking/spiral")) {
    std::cerr << "Full replacement lost a persistent entity.\n";
    return false;
  }
  return true;
}

bool testPreparedDuplicate(const ProjectData &project) {
  std::string error;
  auto incoming =
      loadScene(project, "scene://minimal_2d_networking/platformer", error);
  if (!incoming || incoming->entities.empty())
    return false;
  auto loaded = freshWorld(project);
  if (!loaded)
    return false;
  World world = std::move(*loaded);
  world.ui.nodes.clear();
  world.entities.push_back(incoming->entities.front());
  const std::size_t entityCount = world.entities.size();
  SceneFlow flow;
  ResourceLifetimeRegistry resources;
  flow.configure(project);
  if (!flow.prepare("scene://minimal_2d_networking/platformer", true) ||
      !waitUntilSettled(flow) ||
      flow.activationError(world).find(incoming->entities.front().id) ==
          std::string::npos ||
      flow.activate(world, resources).has_value() ||
      world.entities.size() != entityCount) {
    std::cerr << "Inactive additive scene duplicate was not atomic.\n";
    return false;
  }
  return true;
}

bool testPooledSceneOwnership(const LoadedProject &project) {
  auto loaded = freshWorld(project.project);
  if (!loaded)
    return false;
  World world = std::move(*loaded);
  WorldCommandBuffer commands;
  RuntimePrefabService prefabs;
  prefabs.configure(project.project.projectDirectory);
  const auto instance = prefabs.instantiate(
      world, commands, "prefab://player",
      {.id = "pooled_scene_player", .pooled = true});
  if (!instance)
    return false;
  (void)commands.flush(world);
  if (!prefabs.release(world, commands, instance.instanceId))
    return false;
  (void)commands.flush(world);

  ResourceLifetimeRegistry resources;
  resources.capture(world.activeSceneId, world.entities);
  const Entity *entity = findEntity(world, instance.entityIds.front());
  if (entity == nullptr || entity->enabled ||
      entity->sceneOwner != world.activeSceneId ||
      !resources.owns(world.activeSceneId, "asset://textures/checker"))
    return false;
  std::erase_if(world.entities, [&](const Entity &candidate) {
    return candidate.sceneOwner == world.activeSceneId;
  });
  resources.release(world.activeSceneId);
  prefabs.prune(world);
  if (prefabs.pooledCount("prefab://player") != 0 ||
      resources.groupCount() != 0) {
    std::cerr << "Scene unload retained a stale pooled prefab/resource owner.\n";
    return false;
  }
  return true;
}

bool testScriptFailureAndTeardownCommands() {
  const auto directory = std::filesystem::temp_directory_path() /
                         "demi_lifetime_failure_scripts";
  std::error_code filesystemError;
  std::filesystem::remove_all(directory, filesystemError);
  if (!writeFile(directory / "scripts/outgoing.lua", R"lua(
local Outgoing = {}
function Outgoing:on_destroy()
  Entity.create("ent_teardown_ghost", { components = {} })
end
return Outgoing
)lua") ||
      !writeFile(directory / "scripts/failing.lua", R"lua(
local Failing = {}
function Failing:on_start()
  error("injected activation exception")
end
return Failing
)lua") ||
      !writeFile(directory / "target.scene.json", R"json({
  "format_version": 1,
  "id": "scene://fault/target",
  "entities": [{
    "id": "ent_failing",
    "name": "Failing",
    "components": {
      "LuaScript": { "module": "script://scripts/failing.lua" }
    }
  }]
})json"))
    return false;

  ProjectData project;
  project.projectDirectory = directory;
  project.scenes = {
      {.id = "scene://fault/target", .path = "target.scene.json"}};
  World world;
  world.activeSceneId = "scene://fault/outgoing";
  world.loadedSceneIds.insert(world.activeSceneId);
  std::string error;
  auto outgoing = RuntimeObjectModel::buildEntity(
      {{"id", "ent_outgoing"},
       {"components",
        {{"LuaScript",
          {{"module", "script://scripts/outgoing.lua"}}}}}},
      error);
  if (!outgoing)
    return false;
  outgoing->sceneOwner = world.activeSceneId;
  world.entities.push_back(std::move(*outgoing));

  InputState input;
  LuaScriptHost host;
  if (!host.initialize(world, input, nullptr, error) ||
      !host.loadWorldScripts(project, world, error))
    return false;
  host.start();
  if (!host.prepareScene("scene://fault/target", false))
    return false;
  for (int attempt = 0; attempt < 1000 && !host.scenePrepared(); ++attempt)
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  if (!host.requestPreparedSceneActivation() ||
      !host.applyPendingSceneLoad(error) ||
      world.activeSceneId != "scene://fault/target" ||
      findEntity(world, "ent_failing") == nullptr ||
      findEntity(world, "ent_teardown_ghost") != nullptr) {
    std::cerr << "Script failure or teardown command escaped activation: "
              << error << '\n';
    return false;
  }
  host.destroy();
  return true;
}

bool testRepeatedCycles(const ProjectData &project) {
  int iterations = 16;
  if (const char *configured = std::getenv("DEMI_LIFETIME_STRESS_ITERATIONS"))
    iterations = std::max(std::atoi(configured), 1);
  for (int iteration = 0; iteration < iterations; ++iteration) {
    auto loaded = freshWorld(project);
    if (!loaded)
      return false;
    World world = std::move(*loaded);
    world.ui.nodes.clear();
    SceneFlow flow;
    ResourceLifetimeRegistry resources;
    flow.configure(project);
    if (!flow.prepare("scene://minimal_2d_networking/platformer", true) ||
        !waitUntilSettled(flow) || !flow.activate(world, resources) ||
        !flow.unload(world, "scene://minimal_2d_networking/platformer",
                     resources) ||
        world.loadedSceneIds.size() != 1 || resources.groupCount() != 0) {
      std::cerr << "Repeated lifecycle failed at iteration " << iteration
                << ".\n";
      return false;
    }
  }
  return true;
}

} // namespace

int main() {
  const std::filesystem::path root = std::filesystem::path(DEMI_SOURCE_DIR);
  std::string error;
  auto networking = loadProject(
      root / "examples/minimal_2d_networking/demi.project.json", error);
  auto minimal3D =
      loadProject(root / "examples/minimal_3d/demi.project.json", error);
  if (!networking || !minimal3D) {
    std::cerr << "Could not load lifetime fixtures: " << error << '\n';
    return 1;
  }

  if (!testTransactionalResources() ||
      !testPreparationFailures(networking->project) ||
      !testPersistentReplacement(networking->project) ||
      !testPreparedDuplicate(networking->project) ||
      !testPooledSceneOwnership(*minimal3D) ||
      !testScriptFailureAndTeardownCommands() ||
      !testRepeatedCycles(networking->project))
    return 1;
  return 0;
}
