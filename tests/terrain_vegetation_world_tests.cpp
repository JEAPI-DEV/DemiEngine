#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/terrain/TerrainCellPreparation.h"
#include "demi/runtime/terrain/TerrainScatterBatches.h"
#include "demi/runtime/terrain/TerrainVegetationWorld.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace demi::runtime;
using namespace std::chrono_literals;
namespace {
constexpr auto palette = "asset://test/vegetation";
struct Project {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("demi-vegetation-world-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  Project() {
    std::filesystem::create_directories(path / "prefabs");
    std::ofstream(path / "demi.project.json")
        << R"({"format_version":1,"name":"Vegetation cells"})";
    std::ofstream(path / "prefabs/tree.prefab.json")
        << R"({"format_version":1,"id":"prefab://tree","entities":[{"id":"root","components":{"Transform3D":{},"SphereCollider3D":{"radius":1},"MeshRenderer":{"shape":"sphere"}}}]})";
  }
  ~Project() { std::filesystem::remove_all(path); }
};

template <typename Predicate> void until(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() > deadline) {
      throw std::runtime_error("Vegetation world test timed out");
    }
    std::this_thread::sleep_for(1ms);
  }
}

World makeWorld() {
  World world;
  Entity root;
  root.id = "region";
  root.sceneOwner = "scene://test/landscape";
  root.setComponent(Transform3DComponent{.position = {100, 0, 200}});
  root.setComponent(Terrain3DComponent{});
  world.entities.push_back(std::move(root));
  Entity unrelated;
  unrelated.id = "gameplay";
  world.entities.push_back(std::move(unrelated));
  return world;
}

TerrainScatterResult placements(bool broken = false) {
  TerrainScatterResult result;
  for (int index = 0; index < 2; ++index) {
    TerrainScatterPlacement grass;
    grass.ruleId = "grass";
    grass.model = "asset://test/grass";
    grass.collision = TerrainCollisionPolicy::None;
    grass.position = {float(index), 0, 0};
    grass.candidateX = index;
    result.placements.push_back(std::move(grass));
  }
  TerrainScatterPlacement tree;
  tree.ruleId = "trees";
  tree.prefab = broken ? "prefab://missing" : "prefab://tree";
  tree.collision = TerrainCollisionPolicy::Static;
  tree.position = {4, 0, 0};
  tree.candidateX = 4;
  result.placements.push_back(std::move(tree));
  return result;
}

std::size_t batchCount(const World &world) {
  return std::count_if(
      world.entities.begin(), world.entities.end(), [](const auto &entity) {
        return entity.template hasComponent<TerrainScatterBatch>();
      });
}

void publicationAndEviction() {
  Project project;
  RuntimePrefabService prefabs;
  prefabs.configure(project.path);
  JobSystem jobs(1);
  std::atomic<int> loads = 0;
  TerrainVegetationWorld cells(
      jobs, {.maximumBytes = 1024 * 1024},
      [&](const auto &, std::stop_token) {
        ++loads;
        return placements();
      },
      "region", palette, project.path);
  auto world = makeWorld();
  const auto firstId = terrainVegetationCellOwnerId("region", -1, 0);
  const auto secondId = terrainVegetationCellOwnerId("region", 0, 0);
  std::vector<TerrainVegetationWorld::Demand> demand{
      {{{-1, 0, 1, TerrainVegetationStorage::Procedural}, 65536}, {0, 0, 0}},
      {{{0, 0, 1, TerrainVegetationStorage::Stored}, 65536}, {32, 0, 0}}};
  std::string error;
  until([&] {
    if (!cells.update(world, demand, &prefabs, error)) {
      throw std::runtime_error(error);
    }
    return batchCount(world) == 2;
  });
  assert(loads == 2);
  assert(world.entities.size() ==
         8); // root, unrelated, 2 * (cell, batch, tree)
  for (const auto &entity : world.entities) {
    if (entity.id.starts_with("region/")) {
      assert(entity.sceneOwner == "scene://test/landscape");
    }
    if (const auto *instances = entity.component<MeshInstances3DComponent>()) {
      assert(instances->transforms().size() == 2);
    }
  }
  auto &physics = ensurePhysicsWorld3D(world);
  assert(physics.available());
  physics.step(world, 1.F / 60);
  assert(physics.overlapSphere({104, 0, 200}, 0.5F).size() == 1);
  assert(physics.overlapSphere({136, 0, 200}, 0.5F).size() == 1);
  const auto firstTree =
      terrainScatterInstanceId(firstId, palette, "trees", 4, 0);
  const auto secondTree =
      terrainScatterInstanceId(secondId, palette, "trees", 4, 0);
  assert(prefabs.sourcePrefab(firstTree));
  for (int frame = 0; frame < 100; ++frame) {
    assert(cells.update(world, demand, &prefabs, error));
  }
  assert(loads == 2);
  demand.erase(demand.begin());
  assert(cells.update(world, demand, &prefabs, error));
  physics.step(world, 1.F / 60);
  assert(physics.overlapSphere({104, 0, 200}, 0.5F).empty());
  assert(physics.overlapSphere({136, 0, 200}, 0.5F).size() == 1);
  assert(!prefabs.sourcePrefab(firstTree));
  assert(prefabs.sourcePrefab(secondTree));
  assert(!findEntity(world, firstId));
  assert(batchCount(world) == 1);

  demand.push_back(
      {{{-1, 0, 1, TerrainVegetationStorage::Procedural}, 65536}, {0, 0, 0}});
  until([&] {
    assert(cells.update(world, demand, &prefabs, error));
    return findEntity(world, firstId) != nullptr;
  });
  assert(loads == 3);
  assert(prefabs.sourcePrefab(firstTree));
  physics.step(world, 1.F / 60);
  assert(physics.overlapSphere({104, 0, 200}, 0.5F).size() == 1);
  assert(cells.clear(world, &prefabs, error));
  physics.step(world, 1.F / 60);
  assert(physics.overlapSphere({120, 0, 200}, 64).empty());
  assert(world.entities.size() == 2);
  assert(findEntity(world, "gameplay"));
  assert(!prefabs.sourcePrefab(firstTree) && !prefabs.sourcePrefab(secondTree));
}

void replacementFailureAndOverrides() {
  Project project;
  RuntimePrefabService prefabs;
  prefabs.configure(project.path);
  JobSystem jobs(1);
  TerrainVegetationWorld cells(
      jobs, {.maximumBytes = 1024 * 1024},
      [](const auto &key, std::stop_token) {
        return placements(key.revision == 2);
      },
      "region", palette, project.path);
  auto world = makeWorld();
  std::string error;
  TerrainVegetationWorld::Demand demand{{{0, 0, 1}, 65536}, {}};
  const auto cellId = terrainVegetationCellOwnerId("region", 0, 0);
  const auto grassId = terrainScatterInstanceId(cellId, palette, "grass", 0, 0);
  const auto treeId = terrainScatterInstanceId(cellId, palette, "trees", 4, 0);
  findEntity(world, "region")
      ->component<Terrain3DComponent>()
      ->scatterExclusions = {grassId};
  until([&] {
    assert(cells.update(world, std::span(&demand, 1), &prefabs, error));
    return findEntity(world, cellId) != nullptr;
  });
  for (const auto &entity : world.entities) {
    if (const auto *batch = entity.component<MeshInstances3DComponent>()) {
      assert(batch->transforms().size() == 1);
      assert(!batch->transforms().contains(grassId));
    }
  }
  const auto count = world.entities.size();
  findEntity(world, treeId + "/root")->tags.insert("live_state");
  demand.residency.key.revision = 2;
  until([&] {
    return !cells.update(world, std::span(&demand, 1), &prefabs, error);
  });
  assert(!error.empty());
  assert(world.entities.size() == count);
  assert(findEntity(world, cellId)
             ->component<TerrainVegetationCellOwner>()
             ->key.revision == 1);
  assert(prefabs.sourcePrefab(treeId) == "prefab://tree");
  demand.residency.key.revision = 3;
  demand.origin = {64, 0, 0};
  until([&] {
    assert(cells.update(world, std::span(&demand, 1), &prefabs, error));
    return findEntity(world, cellId)
               ->component<TerrainVegetationCellOwner>()
               ->key.revision == 3;
  });
  assert(findEntity(world, treeId + "/root")->tags.contains("live_state"));
  auto &physics = ensurePhysicsWorld3D(world);
  physics.step(world, 1.F / 60);
  assert(physics.overlapSphere({168, 0, 200}, 0.5F).size() == 1);
  assert(physics.overlapSphere({104, 0, 200}, 0.5F).empty());
  findEntity(world, "region")->enabled = false;
  assert(cells.update(world, std::span(&demand, 1), &prefabs, error));
  assert(world.entities.size() == 2);
  assert(!prefabs.sourcePrefab(treeId));
}
void ownerRemovalAndReservedIdCollision() {
  JobSystem jobs(1);
  TerrainVegetationWorld cells(
      jobs, {.maximumBytes = 1024 * 1024},
      [](const auto &, std::stop_token) {
        auto result = placements();
        result.placements.pop_back();
        return result;
      },
      "region", palette);
  auto world = makeWorld();
  std::string error;
  TerrainVegetationWorld::Demand demand{{{0, 0, 1}, 65536}, {}};
  const auto id = terrainVegetationCellOwnerId("region", 0, 0);
  Entity authored;
  authored.id = id;
  authored.name = "User entity must survive";
  world.entities.push_back(std::move(authored));
  until([&] {
    return !cells.update(world, std::span(&demand, 1), nullptr, error);
  });
  assert(error.find("already owned") != std::string::npos);
  assert(findEntity(world, id)->name == "User entity must survive");
  assert(world.entities.size() == 3);
  WorldCommandBuffer commands;
  assert(commands.destroy(world, id));
  (void)commands.flush(world);
  until([&] {
    assert(cells.update(world, std::span(&demand, 1), nullptr, error));
    return batchCount(world) == 1;
  });
  assert(commands.destroy(world, "region"));
  (void)commands.flush(world);
  assert(cells.update(world, std::span(&demand, 1), nullptr, error));
  assert(world.entities.size() == 1);
  assert(findEntity(world, "gameplay"));
}
void preparedVisualsPreserveFiniteOutput() {
  Project project;
  std::ofstream(project.path / "prefabs/plant.prefab.json")
      << R"({"format_version":1,"id":"prefab://plant","entities":[{"id":"root","components":{"Transform3D":{"position":[1,2,3],"rotation":[0,25,0]}},"children":[{"id":"leaf","components":{"Transform3D":{"position":[0,2,0]},"MeshRenderer":{"shape":"cube","medium_lod_model":"asset://test/low","medium_lod_distance":40,"casts_shadows":false}}}]}]})";
  auto records = placements().placements;
  for (auto &record : records) {
    record.prefab = "prefab://plant";
    record.model.clear();
    record.collision = TerrainCollisionPolicy::None;
    record.yaw = 37;
    record.scale = 1.7F;
  }
  const auto input = records;
  RuntimePrefabService prefabs;
  prefabs.configure(project.path);
  auto finite = makeWorld();
  std::string error;
  WorldCommandBuffer commands;
  const auto result = syncTerrainScatter(
      finite, commands, "region", TerrainScatterView{palette, input, {}},
      [&] { return &prefabs; }, error);
  if (!error.empty()) {
    throw std::runtime_error(error);
  }
  assert(result.created == input.size());
  (void)commands.flush(finite);
  const auto prepared =
      prepareTerrainCell(records, "region", palette, project.path, {});
  assert(prepared && records.empty() && prepared->accountedBytes > 0);
  auto adopted = makeWorld();
  publishTerrainVisuals(adopted, commands, *findEntity(adopted, "region"),
                        *prepared, {});
  (void)commands.flush(adopted);
  assert(adopted.entities.size() == finite.entities.size());
  for (const auto &batch : finite.entities) {
    if (!batch.hasComponent<TerrainScatterBatch>()) {
      continue;
    }
    const auto *same = findEntity(adopted, batch.id);
    assert(same && same->serializedComponents == batch.serializedComponents);
    nlohmann::json before, after;
    MeshInstances3DComponent::readTransforms(
        *batch.component<MeshInstances3DComponent>(), before);
    MeshInstances3DComponent::readTransforms(
        *same->component<MeshInstances3DComponent>(), after);
    assert(before == after);
  }
  std::stop_source cancelled;
  cancelled.request_stop();
  records = input;
  assert(!prepareTerrainCell(records, "region", palette, project.path,
                             cancelled.get_token()));
  assert(records.size() == input.size());
}
} // namespace

int main() {
  preparedVisualsPreserveFiniteOutput();
  publicationAndEviction();
  replacementFailureAndOverrides();
  ownerRemovalAndReservedIdCollision();
  std::cout << "Terrain vegetation world tests passed\n";
}
