#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/terrain/RegionalTerrainCover.h"
#include "demi/runtime/terrain/RegionalTerrainPresentation.h"
#include "demi/runtime/terrain/RegionalTerrainSystem.h"
#include "demi/runtime/terrain/RegionalTerrainVegetation.h"
#include "demi/runtime/terrain/TerrainScatterBatches.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <iostream>
#include <set>
#include <thread>
using namespace demi::runtime;
namespace {
const TerrainRegionHeightBounds flatBounds = [](TerrainRegionPatchKey) {
  return std::pair{0.F, 0.F};
};
TerrainRegionConfig config() {
  TerrainRegionConfig result;
  result.recipe.size = {512, 384};
  result.recipe.chunkCells = 8;
  result.recipe.landforms.at("default").baseHeight = 0;
  result.recipe.landforms.at("default").heightVariation = 0;
  result.originX = -256;
  result.originZ = -192;
  result.baseSampleSpacing = 2;
  result.normalSampleSpacing = 1;
  return result;
}
void coverageAndSeams() {
  auto source = config();
  std::vector<TerrainRegionPatchKey> cover{{0, 0, 0}};
  for (int iteration = 0; iteration < 1000; ++iteration) {
    double area = 0;
    for (const auto key : cover) {
      const auto rectangle = regionPatchRectangle(source, key);
      area += rectangle.width * rectangle.depth;
    }
    assert(area == double(source.recipe.size.x) * source.recipe.size.y);
    (void)regionCoverVariants(cover);
    const auto next =
        advanceRegionCover(source, cover, {0, 0, 0}, flatBounds, 2);
    if (!next) {
      break;
    }
    cover = *next;
    assert(iteration != 999);
  }
  assert(cover.size() > 4 && cover.size() < 512);
  for (int iteration = 0; iteration < 1000; ++iteration) {
    const auto next =
        advanceRegionCover(source, cover, {100000, 0, 100000}, flatBounds, 2);
    if (!next) {
      break;
    }
    cover = *next;
  }
  assert(cover.size() == 1 && cover.front().level == 0);
  source.recipe.landforms.at("default").heightVariation = 40;
  RegionalTerrainSampler sampler(source);
  const auto coarse = sampler.samplePatch({1, 0, 0});
  const auto extrema = std::minmax_element(coarse.field.heights.begin(), coarse.field.heights.end());
  assert(sampler.patchHeightBounds({1, 0, 0}) == std::pair(*extrema.first, *extrema.second));
  const auto initial = planInitialRegionCover(sampler, {0, 0, 0}, 64);
  assert(initial.size() > 1 && initial.size() <= 64);
  (void)regionCoverVariants(initial);
  double initialArea = 0;
  for (const auto key : initial) {
    const auto box = regionPatchRectangle(source, key);
    initialArea += box.width * box.depth;
  }
  assert(initialArea == double(source.recipe.size.x) * source.recipe.size.y);
  const auto fine = sampler.samplePatch({2, 2, 0}, 1);
  for (int z = 0; z <= 8; ++z) {
    const float expected = (z % 2 == 0) ? coarse.field.height(8, z / 2)
                                        : (coarse.field.height(8, z / 2) +
                                           coarse.field.height(8, z / 2 + 1)) *
                                              .5F;
    assert(fine.field.height(0, z) == expected);
  }
  std::stop_source cancelled;
  cancelled.request_stop();
  bool stopped = false;
  try {
    (void)sampler.samplePatch({1, 0, 0}, 0, cancelled.get_token());
  } catch (const std::runtime_error &) {
    stopped = true;
  }
  assert(stopped);
}
void stationaryMountainSelection() {
  auto source = config();
  source.recipe.landforms.at("default").heightVariation = 300;
  RegionalTerrainSampler sampler(source);
  std::map<TerrainRegionPatchKey, std::pair<float, float>> ranges;
  const TerrainRegionHeightBounds bounds = [&](TerrainRegionPatchKey key) {
    auto found = ranges.find(key);
    if (found == ranges.end()) {
      const auto patch = sampler.samplePatch(key);
      const auto [low, high] = std::minmax_element(patch.field.heights.begin(),
                                                   patch.field.heights.end());
      found = ranges.emplace(key, std::pair{*low, *high}).first;
    }
    return found->second;
  };
  std::vector<TerrainRegionPatchKey> cover{{0, 0, 0}};
  std::set<std::vector<TerrainRegionPatchKey>> visited;
  for (int iteration = 0; iteration < 1000; ++iteration) {
    assert(visited.insert(cover).second);
    const auto next = advanceRegionCover(source, cover, {32, 100, 24}, bounds,
                                         2, Vec3{0, 0, 1});
    if (!next) {
      assert(cover.size() > 4);
      for (int repeat = 0; repeat < 20; ++repeat) {
        assert(!advanceRegionCover(source, cover, {32, 100, 24}, bounds, 2,
                                   Vec3{0, 0, 1}));
      }
      return;
    }
    cover = *next;
  }
  assert(false && "Stationary mountain LOD did not settle");
}
void elevatedForegroundPriority() {
  auto source = config();
  source.recipe.size = {512, 512};
  source.originX = 0;
  source.originZ = 0;
  const std::vector<TerrainRegionPatchKey> cover{
      {1, 0, 0}, {1, 0, 1}, {1, 1, 0}, {1, 1, 1}};
  const TerrainRegionHeightBounds bounds = [](TerrainRegionPatchKey key) {
    return key.x == 1 ? std::pair{900.F, 1100.F} : std::pair{0.F, 0.F};
  };
  const auto next = advanceRegionCover(source, cover, {255, 1000, 128}, bounds,
                                       2, Vec3{1, 0, 0});
  assert(next);
  // The mountain beside the viewer must refine even though ground directly
  // underneath the viewer is a kilometre lower.
  assert(std::ranges::find(*next, TerrainRegionPatchKey{1, 1, 0}) ==
         next->end());
  assert(std::ranges::find(*next, TerrainRegionPatchKey{1, 0, 0}) !=
         next->end());
}
void nativeLifetime() {
  auto source = std::make_shared<const TerrainRegionConfig>(config());
  World world;
  Entity root;
  root.id = "region";
  root.setComponent(Transform3DComponent{});
  Terrain3DComponent terrain;
  terrain.region = source;
  root.setComponent(terrain);
  world.entities.push_back(std::move(root));
  auto &physics = ensurePhysicsWorld3D(world);
  RegionalTerrainSystem streaming;
  std::string error;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(15);
  bool appeared = false;
  for (int frame = 0; frame < 700; ++frame) {
    if (frame % 3 == 0) {
      const Vec3 camera =
          frame >= 240 && frame < 450 ? Vec3{20000, 100, 20000} : Vec3{};
      if (!streaming.update(world, camera, error)) {
        throw std::runtime_error(error);
      }
      physics.step(world, 1.F / 60);
    }
    // Queries also run on render-only ticks between fixed steps.
    if (world.entities.size() > 1) {
      appeared = true;
    }
    if (appeared) {
      const auto hit = physics.raycast({0, 100, 0}, {0, -1, 0}, 200);
      assert(hit && std::abs(hit->point.y) < .01F);
    }
    assert(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(appeared && world.entities.size() > 5);
  world.entities.eraseIf(
      [](const auto &entity) { return entity.id == "region"; });
  assert(streaming.update(world, {0, 0, 0}, error));
  assert(world.entities.empty());
  // Retirement must remove bodies even if physics is paused and never stepped.
  assert(!physics.raycast({0, 100, 0}, {0, -1, 0}, 200));
}
void stationaryViewerPublication() {
  auto source = config();
  source.recipe.landforms.at("default").heightVariation = 300;
  source.streaming.maximumResidentBytes = 16 * 1024 * 1024;
  World world;
  Entity root;
  root.id = "translated_region";
  root.setComponent(Transform3DComponent{.position = {10000, 500, -20000}});
  Terrain3DComponent terrain;
  terrain.region = std::make_shared<const TerrainRegionConfig>(source);
  root.setComponent(terrain);
  world.entities.push_back(std::move(root));
  RegionalTerrainSystem streaming({.physics = false, .vegetation = false});
  assert(regionalTerrainPreparing(world));
  std::string error;
  const auto update = [&] {
    assert(streaming.updateView(world, {10032, 600, -19976}, Vec3{0, 0, 1},
                                error));
  };
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(15);
  do {
    update();
    assert(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (streaming.hasPendingWork());
  assert(!regionalTerrainPreparing(world));
  std::vector<std::string> settled;
  for (const auto &entity : world.entities) {
    settled.push_back(entity.id);
  }
  assert(settled.size() > 5);
  for (int frame = 0; frame < 120; ++frame) {
    update();
    assert(!streaming.hasPendingWork());
    std::vector<std::string> current;
    for (const auto &entity : world.entities) {
      current.push_back(entity.id);
    }
    assert(current == settled);
  }
}
void regionalVegetationRevisit() {
  auto source = config();
  source.recipe.landforms.at("default").baseHeight = 100;
  source.recipe.paletteId = "asset://test/palette";
  source.streaming.vegetationCellSamples = 8;
  source.streaming.vegetationRadius = 12;
  auto palette = std::make_shared<TerrainPalette>();
  palette->id = source.recipe.paletteId;
  TerrainPaletteEntry grass;
  grass.model = "asset://test/grass";
  grass.category = TerrainScatterCategory::Grass;
  grass.collision = TerrainCollisionPolicy::None;
  grass.candidateSpacing = 4;
  grass.density = 0.06F;
  grass.positionJitter = 1;
  palette->placements["grass"] = grass;
  source.palette = palette;
  const auto sampler = std::make_shared<const RegionalTerrainSampler>(source);
  JobSystem jobs{2};
  World world;
  Entity root;
  root.id = "region";
  root.setComponent(Transform3DComponent{});
  world.entities.push_back(std::move(root));
  RegionalTerrainVegetation vegetation(jobs, sampler, "region");
  std::string error;
  const auto settle = [&](Vec3 camera) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    for (;;) {
      if (!vegetation.update(world, camera, nullptr, error)) {
        throw std::runtime_error(error);
      }
      const auto stats = vegetation.statistics();
      if (stats.residentCells == 9 && stats.pendingJobs == 0) {
        break;
      }
      assert(std::chrono::steady_clock::now() < deadline);
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  };
  const auto signature = [&] {
    std::map<std::string, std::array<float, 3>> result;
    for (const auto &entity : world.entities) {
      if (const auto *instances =
              entity.component<MeshInstances3DComponent>()) {
        for (const auto &[id, pose] : instances->transforms()) {
          result[entity.id + "/" + id] = {pose.position.x, pose.position.y,
                                          pose.position.z};
        }
      }
    }
    return result;
  };
  settle({8, 100, 8});
  const auto first = signature();
  assert(!first.empty());
  assert(vegetation.clear(world, nullptr, error));
  assert(world.entities.size() == 1);
  // Draining a retired owner must not evict its replacement's live cells.
  RegionalTerrainVegetation replacement(jobs, sampler, "region");
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  while (replacement.statistics().residentCells != 9) {
    assert(replacement.update(world, {8, 100, 8}, nullptr, error));
    vegetation.drain();
    assert(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  assert(signature() == first);
  assert(replacement.clear(world, nullptr, error));
  settle({120, 100, 120});
  assert(signature() != first);
  settle({8, 100, 8});
  assert(signature() == first);
}
void independentVegetationRanges() {
  auto source = config();
  source.recipe.landforms.at("default").baseHeight = 100;
  source.recipe.paletteId = "asset://test/palette";
  source.streaming.vegetationCellSamples = 8;
  source.streaming.vegetationRadius = 12;
  source.streaming.vegetationCanopyCellSamples = 16;
  source.streaming.vegetationCanopyRadius = 80;
  auto palette = std::make_shared<TerrainPalette>();
  palette->id = source.recipe.paletteId;
  TerrainPaletteEntry grass;
  grass.model = "asset://test/grass";
  grass.category = TerrainScatterCategory::Grass;
  grass.collision = TerrainCollisionPolicy::None;
  grass.candidateSpacing = 4;
  grass.density = 0.06F;
  palette->placements["grass"] = grass;
  auto tree = grass;
  tree.model = "asset://test/tree";
  tree.category = TerrainScatterCategory::Tree;
  palette->placements["tree"] = tree;
  source.palette = palette;
  JobSystem jobs{2};
  World world;
  Entity root;
  root.id = "region";
  root.setComponent(Transform3DComponent{});
  world.entities.push_back(std::move(root));
  RegionalTerrainVegetation vegetation(
      jobs, std::make_shared<const RegionalTerrainSampler>(source), "region");
  std::string error;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(10);
  do {
    assert(vegetation.update(world, {8, 100, 8}, nullptr, error));
    assert(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (vegetation.hasPendingWork());
  bool grassSeen = false;
  bool distantTreeSeen = false;
  for (const auto &entity : world.entities) {
    const auto *mesh = entity.component<MeshRendererComponent>();
    const auto *instances = entity.component<MeshInstances3DComponent>();
    if (!mesh || !instances) {
      continue;
    }
    const auto *transform = entity.component<Transform3DComponent>();
    assert(transform);
    const auto *cellEntity = findEntity(world, transform->parent);
    assert(cellEntity);
    const auto *cell = cellEntity->component<TerrainVegetationCellOwner>();
    assert(cell);
    const auto origin = cellEntity->component<Transform3DComponent>()->position;
    if (mesh->model == grass.model) {
      grassSeen = true;
      assert(cell->key.layer == 0);
    } else {
      assert(mesh->model == tree.model && cell->key.layer == 1);
      distantTreeSeen |= std::hypot(origin.x - 8, origin.z - 8) > 40;
    }
  }
  assert(grassSeen && distantTreeSeen);
  assert(vegetation.clear(world, nullptr, error));
  assert(world.entities.size() == 1);
}

} // namespace
int main() {
  coverageAndSeams();
  stationaryMountainSelection();
  elevatedForegroundPriority();
  nativeLifetime();
  stationaryViewerPublication();
  regionalVegetationRevisit();
  independentVegetationRanges();
  std::cout << "Regional terrain streaming checks passed\n";
}
