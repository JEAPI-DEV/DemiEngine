#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/RuntimePrefabService.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>

using namespace demi::runtime;
namespace {
struct Project {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("demi-prepared-prefab-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  Project() {
    std::filesystem::create_directories(path / "prefabs");
    std::ofstream(path / "demi.project.json")
        << R"({"format_version":1,"name":"Prefab preparation"})";
    std::ofstream(path / "prefabs/actor.prefab.json")
        << R"({"format_version":1,"id":"prefab://actor","entities":[{"id":"root","components":{"Transform3D":{"position":[1,2,3]}},"children":[{"id":"child","components":{"Transform3D":{"position":[0,2,0]},"SphereCollider3D":{"radius":1}}}]}]})";
  }
  ~Project() { std::filesystem::remove_all(path); }
};

void prepareAndAdopt() {
  Project project;
  RuntimePrefabService live;
  live.configure(project.path);
  World world;
  world.activeSceneId = "scene://activation/owner";
  WorldCommandBuffer commands;
  auto work = std::async(std::launch::async, [path = project.path] {
    RuntimePrefabService worker;
    worker.configure(path);
    auto prepared = worker.prepareInstance(
        "prefab://actor", {.id = "spawn", .position = Vec3{5, 6, 7}});
    assert(!worker.sourcePrefab("spawn"));
    return prepared;
  });
  auto prepared = work.get();
  assert(prepared && world.entities.empty() && commands.empty());
  assert(prepared.entities().size() == 2);
  assert(prepared.entities()[0].component<Transform3DComponent>()->position.x ==
         5);
  assert(prepared.entities()[1].component<Transform3DComponent>()->parent ==
         "spawn/root");
  assert(live.adoptPrepared(world, commands, prepared));
  assert(world.entities.empty());
  assert(live.sourcePrefab("spawn") == "prefab://actor");
  const auto queued = commands.affectedEntityIds();
  assert(!live.adoptPrepared(world, commands, prepared));
  assert(commands.affectedEntityIds() == queued);
  (void)commands.flush(world);
  for (const auto &entity : world.entities) {
    assert(entity.sceneOwner == world.activeSceneId &&
           entity.prefabInstance == "spawn");
  }
  findEntity(world, "spawn/root")
      ->updateComponent<Transform3DComponent>(
          [](auto &transform) { transform.position.x = 99; });
  assert(prepared.entities()[0].component<Transform3DComponent>()->position.x ==
         5);
  assert(live.release(world, commands, "spawn"));
  (void)commands.flush(world);
  assert(world.entities.empty());
  assert(live.adoptPrepared(world, commands, prepared));
  (void)commands.flush(world);
  assert(findEntity(world, "spawn/root")
             ->component<Transform3DComponent>()
             ->position.x == 5);
}

void conflictsAndPooling() {
  Project project, other;
  RuntimePrefabService service;
  service.configure(project.path);
  const auto prepared =
      service.prepareInstance("prefab://actor", {.id = "item"});
  assert(prepared);
  World world;
  Entity conflicting;
  conflicting.id = "item/child";
  conflicting.name = "Unrelated entity";
  world.entities.push_back(std::move(conflicting));
  WorldCommandBuffer commands;
  assert(!service.adoptPrepared(world, commands, prepared));
  assert(commands.empty() && !service.sourcePrefab("item"));
  world.entities.clear();
  Entity pending;
  pending.id = "item/child";
  assert(commands.create(world, std::move(pending)));
  const auto before = commands.affectedEntityIds();
  assert(!service.adoptPrepared(world, commands, prepared));
  assert(commands.affectedEntityIds() == before &&
         !service.sourcePrefab("item"));
  commands.clear();
  RuntimePrefabService wrongProject;
  wrongProject.configure(other.path);
  const auto wrong = wrongProject.adoptPrepared(world, commands, prepared);
  assert(!wrong &&
         wrong.diagnostics.back().code == "PREFAB_RUNTIME_PROJECT_MISMATCH");
  assert(commands.empty());
  const auto invalid =
      service.prepareInstance("prefab://missing", {.id = "invalid"});
  assert(!invalid && !service.adoptPrepared(world, commands, invalid));
  assert(commands.empty());
  const auto pooled = service.prepareInstance("prefab://actor",
                                              {.id = "pooled", .pooled = true});
  assert(service.adoptPrepared(world, commands, pooled));
  (void)commands.flush(world);
  assert(service.release(world, commands, "pooled"));
  (void)commands.flush(world);
  assert(service.pooledCount("prefab://actor") == 1);
  const auto reused = service.instantiate(world, commands, "prefab://actor",
                                          {.id = "ignored", .pooled = true});
  assert(reused && reused.instanceId == "pooled");
  (void)commands.flush(world);
  assert(service.pooledCount("prefab://actor") == 0);
  for (const auto &entity : world.entities) {
    assert(entity.enabled);
  }
}
} // namespace
int main() {
  RuntimeProfiler::setEnabled(true);
  prepareAndAdopt();
  conflictsAndPooling();
  std::cout << "Prefab preparation/adoption tests passed\n";
}
