#include "demi/runtime/scene/RuntimePrefabService.h"
#include "demi/runtime/scene/SceneFlow.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <thread>

using namespace demi::runtime;

namespace {

bool waitUntilPrepared(SceneFlow &flow) {
  for (int attempt = 0; attempt < 500; ++attempt) {
    flow.poll();
    if (flow.state() != ScenePreparationState::Loading)
      return flow.state() == ScenePreparationState::Ready;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

} // namespace

int main() {
  const std::filesystem::path root = std::filesystem::path(DEMI_SOURCE_DIR);

  std::string error;
  auto minimal3D =
      loadProject(root / "examples/minimal_3d/demi.project.json", error);
  if (!minimal3D) {
    std::cerr << "Could not load prefab test project: " << error << '\n';
    return 1;
  }

  RuntimePrefabService prefabs;
  prefabs.configure(minimal3D->project.projectDirectory);
  WorldCommandBuffer commands;
  const auto spawned =
      prefabs.instantiate(minimal3D->world, commands, "prefab://player",
                          {.id = "runtime_player",
                           .position = Vec3{.x = 4.0F, .y = 2.0F, .z = 3.0F},
                           .pooled = true});
  if (!spawned ||
      spawned.entityIds != std::vector<std::string>{"runtime_player/body"}) {
    std::cerr
        << "Runtime prefab expansion did not preserve stable local ids.\n";
    return 1;
  }
  (void)commands.flush(minimal3D->world);
  const Entity *player = findEntity(minimal3D->world, "runtime_player/body");
  if (player == nullptr || player->prefabInstance != "runtime_player" ||
      player->prefabLocalId != "body" ||
      player->component<Transform3DComponent>()->position.x != 4.0F) {
    std::cerr << "Runtime prefab identity or root placement was incorrect.\n";
    return 1;
  }
  const auto *playerBox = player->component<BoxCollider3DComponent>();
  if (playerBox == nullptr ||
      !player->hasComponent<CharacterController3DComponent>() ||
      player->hasComponent<Rigidbody3DComponent>() ||
      playerBox->size.x != 1.0F || playerBox->size.y != 1.0F ||
      playerBox->size.z != 1.0F) {
    std::cerr << "Minimal 3D cube does not use a matching box collider.\n";
    return 1;
  }
  if (!prefabs.release(minimal3D->world, commands, "runtime_player")) {
    std::cerr << "Pooled prefab release failed.\n";
    return 1;
  }
  (void)commands.flush(minimal3D->world);
  if (findEntity(minimal3D->world, "runtime_player/body")->enabled ||
      prefabs.pooledCount("prefab://player") != 1) {
    std::cerr << "Pooled prefab was not retained in a disabled state.\n";
    return 1;
  }
  const auto reused =
      prefabs.instantiate(minimal3D->world, commands, "prefab://player",
                          {.id = "ignored_for_pool",
                           .position = Vec3{.x = 8.0F, .y = 1.0F, .z = 2.0F},
                           .pooled = true});
  (void)commands.flush(minimal3D->world);
  player = findEntity(minimal3D->world, "runtime_player/body");
  if (!reused || reused.instanceId != "runtime_player" || player == nullptr ||
      !player->enabled ||
      player->component<Transform3DComponent>()->position.x != 8.0F) {
    std::cerr << "Pooled prefab reuse did not reset component state.\n";
    return 1;
  }
  if (!prefabs.release(minimal3D->world, commands, "runtime_player/body")) {
    std::cerr << "Prefab release did not accept an instance entity id.\n";
    return 1;
  }
  (void)commands.flush(minimal3D->world);

  auto networking = loadProject(
      root / "examples/minimal_2d_networking/demi.project.json", error);
  if (!networking) {
    std::cerr << "Could not load scene-flow test project: " << error << '\n';
    return 1;
  }
  SceneFlow scenes;
  scenes.configure(networking->project);
  ResourceLifetimeRegistry resources;
  resources.capture(networking->world.activeSceneId,
                    networking->world.entities);

  if (networking->world.entities.empty()) {
    std::cerr << "Scene-flow fixture has no persistent candidate.\n";
    return 1;
  }
  const std::string persistentId = networking->world.entities.front().id;
  // This fixture's menu and gameplay HUD intentionally reuse "ui_root".
  // Additive scenes require globally stable live IDs, so unload the menu UI
  // before probing a successful gameplay overlay.
  networking->world.ui.nodes.clear();
  if (!scenes.setPersistent(networking->world, persistentId, true) ||
      !scenes.prepare("scene://minimal_2d_networking/platformer", true) ||
      !waitUntilPrepared(scenes) || scenes.progress() != 1.0F) {
    std::cerr << "Asynchronous additive scene preparation failed: "
              << scenes.error() << '\n';
    return 1;
  }
  const auto additive = scenes.activate(networking->world, resources);
  if (!additive || !additive->additive ||
      !networking->world.loadedSceneIds.contains(
          "scene://minimal_2d_networking/platformer")) {
    std::cerr << "Prepared additive scene activation failed.\n";
    return 1;
  }
  const auto unloaded = scenes.unload(
      networking->world, "scene://minimal_2d_networking/platformer", resources);
  if (!unloaded ||
      networking->world.loadedSceneIds.contains(
          "scene://minimal_2d_networking/platformer") ||
      findEntity(networking->world, persistentId) == nullptr) {
    std::cerr << "Additive unload removed persistent state or leaked scene.\n";
    return 1;
  }

  if (!scenes.prepare("scene://minimal_2d_networking/spiral", false) ||
      !waitUntilPrepared(scenes) ||
      !scenes.activate(networking->world, resources) ||
      networking->world.activeSceneId !=
          "scene://minimal_2d_networking/spiral" ||
      findEntity(networking->world, persistentId) == nullptr) {
    std::cerr << "Full scene activation did not preserve persistent entity.\n";
    return 1;
  }

  auto collisionFixture = loadScene(
      networking->project, "scene://minimal_2d_networking/platformer", error);
  if (!collisionFixture || collisionFixture->entities.empty()) {
    std::cerr << "Could not load additive collision fixture.\n";
    return 1;
  }
  const std::string collisionId = collisionFixture->entities.front().id;
  networking->world.entities.push_back(collisionFixture->entities.front());
  if (!scenes.prepare("scene://minimal_2d_networking/platformer", true) ||
      !waitUntilPrepared(scenes) ||
      scenes.activationError(networking->world).find(collisionId) ==
          std::string::npos ||
      scenes.activate(networking->world, resources).has_value()) {
    std::cerr << "Additive scene entity collisions were not rejected "
                 "deterministically.\n";
    return 1;
  }
  return 0;
}
