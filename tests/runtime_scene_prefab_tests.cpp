#include "demi/runtime/physics/PhysicsWorld3D.h"
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

  {
    RuntimePrefabService pruneService;
    pruneService.configure(root / "examples/minimal_3d");
    World pruneWorld;
    WorldCommandBuffer pruneCommands;
    const auto kept =
        pruneService.instantiate(pruneWorld, pruneCommands, "prefab://player",
                                 {.id = "kept", .pooled = true});
    const auto removed =
        pruneService.instantiate(pruneWorld, pruneCommands, "prefab://player",
                                 {.id = "removed", .pooled = true});
    if (!kept || !removed)
      return 1;
    (void)pruneCommands.flush(pruneWorld);
    if (!pruneService.release(pruneWorld, pruneCommands, "kept") ||
        !pruneCommands.destroy(pruneWorld, "removed/body"))
      return 1;
    (void)pruneCommands.flush(pruneWorld);
    pruneService.prune(pruneWorld);
    // Disabled pooled entities still exist and remain reusable. Fully removed
    // instances must no longer reserve their stable instance ID.
    if (pruneService.pooledCount("prefab://player") != 1 ||
        !pruneService.instantiate(pruneWorld, pruneCommands, "prefab://player",
                                  {.id = "removed"})) {
      std::cerr << "Prefab pruning lost a live pool entry or retained a "
                   "removed ID.\n";
      return 1;
    }
  }

  std::string error;
  auto minimal3D =
      loadProject(root / "examples/minimal_3d/demi.project.json", error);
  if (!minimal3D) {
    std::cerr << "Could not load prefab test project: " << error << '\n';
    return 1;
  }
  const Entity *authoredPlayer = findEntity(minimal3D->world, "player/body");
  const Entity *authoredObstacle =
      findEntity(minimal3D->world, "obstacle_1/body");
  if (authoredPlayer == nullptr || authoredPlayer->prefabInstance != "player" ||
      authoredPlayer->prefabLocalId != "body" || authoredObstacle == nullptr ||
      authoredObstacle->prefabInstance != "obstacle_1" ||
      authoredObstacle->prefabLocalId != "body") {
    std::cerr << "Scene-expanded prefab entities lost their authored origin.\n";
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

  // Scene readiness includes immutable native shapes, but no live physics
  // system, bodies or simulation have started in the prepared world.
  SceneFlow prepared3D;
  prepared3D.configure(minimal3D->project);
  if (!prepared3D.prepare(minimal3D->project.mainScene, false) ||
      !waitUntilPrepared(prepared3D)) {
    std::cerr << "3D scene shape preparation failed: " << prepared3D.error()
              << '\n';
    return 1;
  }
  const World *incoming3D = prepared3D.preparedWorld();
  if (incoming3D == nullptr || !incoming3D->preparedPhysicsShapes3D ||
      incoming3D->physicsWorld3D != nullptr) {
    std::cerr
        << "Prepared scene did not retain its shape-only physics cache.\n";
    return 1;
  }
  const auto preparedShapes = incoming3D->preparedPhysicsShapes3D;
  World activated3D;
  ResourceLifetimeRegistry preparedResources;
  if (!prepared3D.activate(activated3D, preparedResources) ||
      activated3D.preparedPhysicsShapes3D != preparedShapes ||
      activated3D.physicsWorld3D != nullptr) {
    std::cerr << "Scene activation did not transfer prepared native shapes.\n";
    return 1;
  }

  // A ready but unpublished cache is released immediately on cancellation.
  if (!prepared3D.prepare(minimal3D->project.mainScene, false) ||
      !waitUntilPrepared(prepared3D)) {
    std::cerr << "Could not prepare the cancellation shape fixture.\n";
    return 1;
  }
  const std::weak_ptr<PreparedPhysicsShapes3D> cancelledCache =
      prepared3D.preparedWorld()->preparedPhysicsShapes3D;
  if (!prepared3D.cancel() || prepared3D.preparedWorld() != nullptr ||
      !cancelledCache.expired() ||
      activated3D.preparedPhysicsShapes3D != preparedShapes) {
    std::cerr
        << "Cancellation retained or published an incoming shape cache.\n";
    return 1;
  }

  // Retaining a test-only reference keeps the cancelled allocation alive, so
  // pointer comparison proves a later prepare owns a different cache.
  if (!prepared3D.prepare(minimal3D->project.mainScene, false) ||
      !waitUntilPrepared(prepared3D)) {
    std::cerr << "Could not prepare the repeated shape fixture.\n";
    return 1;
  }
  const auto rejectedCache =
      prepared3D.preparedWorld()->preparedPhysicsShapes3D;
  if (!prepared3D.cancel() || rejectedCache.use_count() != 1 ||
      !prepared3D.prepare("scene://missing_shape_fixture", false) ||
      waitUntilPrepared(prepared3D) ||
      prepared3D.state() != ScenePreparationState::Failed ||
      prepared3D.error().empty() || prepared3D.preparedWorld() != nullptr ||
      activated3D.preparedPhysicsShapes3D != preparedShapes) {
    std::cerr << "Failed preparation changed the active native shape owner.\n";
    return 1;
  }
  if (!prepared3D.prepare(minimal3D->project.mainScene, false) ||
      !waitUntilPrepared(prepared3D)) {
    std::cerr << "Could not retry shape preparation after failure.\n";
    return 1;
  }
  const auto retryCache = prepared3D.preparedWorld()->preparedPhysicsShapes3D;
  if (!retryCache || retryCache == rejectedCache ||
      !prepared3D.activate(activated3D, preparedResources) ||
      activated3D.preparedPhysicsShapes3D != retryCache) {
    std::cerr << "Retry adopted a cancelled preparation's shape cache.\n";
    return 1;
  }

  World additive3D;
  additive3D.activeSceneId = "scene://prior_physics";
  additive3D.loadedSceneIds.insert(additive3D.activeSceneId);
  Entity retainedEntity;
  retainedEntity.id = "retained_before_additive";
  retainedEntity.sceneOwner = additive3D.activeSceneId;
  additive3D.entities.push_back(std::move(retainedEntity));
  additive3D.physicsWorld3D = std::make_shared<PhysicsWorld3D>();
  const auto activePhysicsOwner = additive3D.physicsWorld3D;
  SceneFlow additiveShapes;
  additiveShapes.configure(minimal3D->project);
  ResourceLifetimeRegistry additiveResources;
  if (!additiveShapes.prepare(minimal3D->project.mainScene, true) ||
      !waitUntilPrepared(additiveShapes) ||
      !additiveShapes.preparedWorld()->preparedPhysicsShapes3D ||
      !additiveShapes.activate(additive3D, additiveResources) ||
      additive3D.physicsWorld3D != activePhysicsOwner ||
      additive3D.preparedPhysicsShapes3D != nullptr ||
      findEntity(additive3D, "retained_before_additive") == nullptr ||
      findEntity(additive3D, "ent_ground") == nullptr) {
    std::cerr << "Additive preparation replaced the existing physics owner.\n";
    return 1;
  }
  activePhysicsOwner->step(additive3D, 1.0F / 60.0F);
  if (additive3D.physicsWorld3D != activePhysicsOwner ||
      !activePhysicsOwner->raycast({0, 8, 0}, {0, -1, 0}, 20)) {
    std::cerr
        << "Additive activation did not adopt incoming collision shapes.\n";
    return 1;
  }

  auto networking = loadProject(
      root / "examples/minimal_2d_android/demi.project.json", error);
  if (!networking) {
    std::cerr << "Could not load scene-flow test project: " << error << '\n';
    return 1;
  }
  SceneFlow scenes;
  scenes.configure(networking->project);
  ResourceLifetimeRegistry resources;
  resources.capture(networking->world.activeSceneId,
                    networking->world.entities);

  // A cancelled worker may still be finishing. A replacement owns its own
  // future, and the cancelled result cannot overwrite the next preparation.
  if (!scenes.prepare("scene://minimal_2d_android/platformer", true) ||
      !scenes.cancel() || scenes.preparedWorld() != nullptr ||
      !scenes.prepare("scene://minimal_2d_android/spiral", false) ||
      !waitUntilPrepared(scenes) ||
      scenes.preparedSceneId() != "scene://minimal_2d_android/spiral" ||
      !scenes.cancel() || scenes.preparedWorld() != nullptr) {
    std::cerr << "Cancelled preparation leaked into its replacement.\n";
    return 1;
  }

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
      !scenes.prepare("scene://minimal_2d_android/platformer", true) ||
      !waitUntilPrepared(scenes) || scenes.progress() != 1.0F) {
    std::cerr << "Asynchronous additive scene preparation failed: "
              << scenes.error() << '\n';
    return 1;
  }
  const World *preparedWorld = scenes.preparedWorld();
  if (preparedWorld == nullptr ||
      preparedWorld->activeSceneId != "scene://minimal_2d_android/platformer" ||
      scenes.preparedSceneId() != "scene://minimal_2d_android/platformer" ||
      networking->world.loadedSceneIds.contains(
          "scene://minimal_2d_android/platformer")) {
    std::cerr
        << "Prepared scene snapshot changed the live world prematurely.\n";
    return 1;
  }
  const auto additive = scenes.activate(networking->world, resources);
  if (!additive || !additive->additive ||
      !networking->world.loadedSceneIds.contains(
          "scene://minimal_2d_android/platformer")) {
    std::cerr << "Prepared additive scene activation failed.\n";
    return 1;
  }
  const auto unloaded = scenes.unload(
      networking->world, "scene://minimal_2d_android/platformer", resources);
  if (!unloaded ||
      networking->world.loadedSceneIds.contains(
          "scene://minimal_2d_android/platformer") ||
      findEntity(networking->world, persistentId) == nullptr) {
    std::cerr << "Additive unload removed persistent state or leaked scene.\n";
    return 1;
  }

  if (!scenes.prepare("scene://minimal_2d_android/spiral", false) ||
      !waitUntilPrepared(scenes) ||
      !scenes.activate(networking->world, resources) ||
      networking->world.activeSceneId != "scene://minimal_2d_android/spiral" ||
      findEntity(networking->world, persistentId) == nullptr) {
    std::cerr << "Full scene activation did not preserve persistent entity.\n";
    return 1;
  }

  auto collisionFixture = loadScene(
      networking->project, "scene://minimal_2d_android/platformer", error);
  if (!collisionFixture || collisionFixture->entities.empty()) {
    std::cerr << "Could not load additive collision fixture.\n";
    return 1;
  }
  const std::string collisionId = collisionFixture->entities.front().id;
  networking->world.entities.push_back(collisionFixture->entities.front());
  if (!scenes.prepare("scene://minimal_2d_android/platformer", true) ||
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
