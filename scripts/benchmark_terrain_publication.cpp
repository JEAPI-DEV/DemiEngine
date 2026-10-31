#include "demi/assets/TerrainPlacementCodec.h"
#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/RuntimePrefabService.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/terrain/TerrainVegetationWorld.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace demi::runtime;
using Clock = std::chrono::steady_clock;
namespace {
// Isolated CPU publication fixture, not Alpine geography or production art.
struct Project {
  std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("demi-publication-probe-" +
       std::to_string(Clock::now().time_since_epoch().count()));
  Project() {
    std::filesystem::create_directories(path / "prefabs");
    std::ofstream(path / "demi.project.json")
        << R"({"format_version":1,"name":"Publication probe"})";
    std::ofstream(path / "prefabs/grass.prefab.json")
        << R"({"format_version":1,"id":"prefab://grass","entities":[{"id":"root","components":{"Transform3D":{},"MeshRenderer":{"shape":"cube"}}}]})";
    std::ofstream(path / "prefabs/tree.prefab.json")
        << R"({"format_version":1,"id":"prefab://tree","entities":[{"id":"root","components":{"Transform3D":{},"MeshRenderer":{"shape":"sphere"},"SphereCollider3D":{"radius":1}}}]})";
  }
  ~Project() { std::filesystem::remove_all(path); }
};

double elapsed(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}

struct Samples {
  std::vector<double> values;
  void report(int metres, const char *mode, std::string_view phase,
              std::size_t placements, std::size_t colliders) {
    std::sort(values.begin(), values.end());
    std::cout << metres << ',' << mode << ',' << placements << ',' << colliders
              << ',' << phase << ',' << values.size() << ','
              << values[values.size() / 2] << ','
              << values[std::min(values.size() - 1,
                                 (values.size() * 95 + 99) / 100 - 1)]
              << ',' << values.back() << '\n';
  }
};

void run(int metres, int repetitions, bool visualOnly, bool framePaced) {
  Project project;
  TerrainRecipe recipe;
  recipe.size = {float(metres), float(metres)};
  recipe.cellsX = recipe.cellsZ = metres / 2;
  recipe.landforms.at("default").baseHeight = 20;
  recipe.landforms.at("default").heightVariation = 3;
  recipe.paletteId = "asset://probe/palette";
  TerrainPalette palette;
  palette.id = recipe.paletteId;
  TerrainPaletteEntry grass;
  grass.prefab = "prefab://grass";
  grass.collision = TerrainCollisionPolicy::None;
  grass.density = 0.8F;
  grass.candidateSpacing = 1.1F;
  grass.positionJitter = 1;
  grass.clusterStrength = 0.6F;
  palette.placements.emplace("grass", grass);
  TerrainPaletteEntry tree;
  tree.prefab = "prefab://tree";
  tree.collision = visualOnly ? TerrainCollisionPolicy::None
                              : TerrainCollisionPolicy::Static;
  tree.density = 0.015F;
  tree.candidateSpacing = 4;
  tree.spacing = 6;
  tree.positionJitter = 1;
  palette.placements.emplace("trees", tree);
  auto prepared = *TerrainGenerator::generate(recipe, &palette);
  if (prepared.scatterTruncated) {
    throw std::runtime_error("Probe was truncated");
  }
  const auto count = prepared.scatterPlacements.size();
  const auto colliders = std::count_if(
      prepared.scatterPlacements.begin(), prepared.scatterPlacements.end(),
      [](const auto &placement) {
        return placement.collision == TerrainCollisionPolicy::Static;
      });
  const auto packed = demi::assets::terrain_payload::encodePlacements(prepared);
  prepared.scatterPlacements.clear();
  prepared.scatterPlacements.shrink_to_fit();
  JobSystem jobs(1);
  for (const auto mode : {TerrainVegetationStorage::Stored,
                          TerrainVegetationStorage::Procedural}) {
    Samples worker, workerPreparation, publication, physicsArrival,
        combinedArrival, readiness, steady, eviction, physicsEviction,
        loadingWork, loadingIntervals;
    std::map<std::string, Samples> profileSamples;
    std::size_t peakOwnedPayload = 0;
    std::size_t loadingOverBudget = 0;
    RuntimePrefabService prefabs;
    prefabs.configure(project.path);
    for (int iteration = -1; iteration < repetitions; ++iteration) {
      World world;
      Entity root;
      root.id = "region";
      root.setComponent(Transform3DComponent{});
      world.entities.push_back(std::move(root));
      auto &physics = ensurePhysicsWorld3D(world);
      if (!physics.available()) {
        throw std::runtime_error("Native physics unavailable");
      }
      physics.step(world, 1.F / 60);
      std::atomic<double> workerMs = 0;
      TerrainVegetationWorld cells(
          jobs, {.maximumBytes = 256U * 1024U * 1024U},
          [&](const auto &key, std::stop_token stop) {
            const auto start = Clock::now();
            TerrainScatterResult result;
            if (key.storage == TerrainVegetationStorage::Procedural) {
              auto field = TerrainGenerator::generate(recipe, &palette, stop);
              if (field) {
                result = {std::move(field->scatterPlacements),
                          field->scatterTruncated};
              }
            } else {
              auto field = prepared;
              demi::assets::terrain_payload::decodePlacements(packed, field);
              result = {std::move(field.scatterPlacements), false};
            }
            workerMs = elapsed(start);
            return result;
          },
          "region", palette.id, project.path);
      TerrainVegetationWorld::Demand demand{
          {{0, 0, 1, mode}, 128U * 1024U * 1024U}, {}};
      std::string error;
      const auto requestStart = Clock::now();
      auto nextTick = requestStart;
      auto lastTick = requestStart;
      bool firstTick = true;
      const auto framePeriod = std::chrono::nanoseconds(16666667);
      for (;;) {
        const auto tick = Clock::now();
        if (framePaced && !firstTick && iteration >= 0) {
          loadingIntervals.values.push_back(
              std::chrono::duration<double, std::milli>(tick - lastTick)
                  .count());
        }
        lastTick = tick;
        firstTick = false;
        RuntimeProfiler::beginFrame();
        const auto start = Clock::now();
        if (!cells.update(world, std::span(&demand, 1), &prefabs, error)) {
          throw std::runtime_error(error);
        }
        const double updateMs = elapsed(start);
        double physicsMs = 0;
        if (framePaced || world.entities.size() > 1) {
          const auto physicsStart = Clock::now();
          physics.step(world, 1.F / 60);
          physicsMs = elapsed(physicsStart);
        }
        if (framePaced && iteration >= 0) {
          const double workMs = elapsed(start);
          loadingWork.values.push_back(workMs);
          loadingOverBudget += workMs > 1000.0 / 60;
        }
        if (world.entities.size() > 1) {
          peakOwnedPayload =
              std::max(peakOwnedPayload, cells.statistics().residentBytes);
          if (iteration >= 0) {
            for (const auto &entry : RuntimeProfiler::frameEntries()) {
              if (entry.name.starts_with("Terrain.")) {
                profileSamples[entry.name].values.push_back(
                    entry.totalMilliseconds);
              }
            }
            worker.values.push_back(workerMs);
            workerPreparation.values.push_back(
                cells.statistics().latestPreparationMilliseconds);
            publication.values.push_back(updateMs);
            physicsArrival.values.push_back(physicsMs);
            combinedArrival.values.push_back(updateMs + physicsMs);
            readiness.values.push_back(elapsed(requestStart));
          }
          break;
        }
        if (elapsed(requestStart) > 30000) {
          throw std::runtime_error("Probe timed out");
        }
        if (framePaced) {
          nextTick += framePeriod;
          std::this_thread::sleep_until(nextTick);
        } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
      }
      std::size_t rendered = 0;
      for (const auto &entity : world.entities) {
        if (const auto *instances =
                entity.component<MeshInstances3DComponent>()) {
          rendered += instances->transforms().size();
        }
      }
      if (rendered + colliders != count ||
          physics.overlapSphere({float(metres) / 2, 20, float(metres) / 2},
                                float(metres) * 2)
                  .size() != std::size_t(colliders)) {
        throw std::runtime_error("Published placement or body count mismatch");
      }
      for (int frame = 0; frame < 20; ++frame) {
        const auto start = Clock::now();
        if (!cells.update(world, std::span(&demand, 1), &prefabs, error)) {
          throw std::runtime_error(error);
        }
        physics.step(world, 1.F / 60);
        if (iteration >= 0) {
          steady.values.push_back(elapsed(start));
        }
      }
      const auto removalStart = Clock::now();
      if (!cells.clear(world, &prefabs, error)) {
        throw std::runtime_error(error);
      }
      const auto removalMs = elapsed(removalStart);
      const auto physicsStart = Clock::now();
      physics.step(world, 1.F / 60);
      if (iteration >= 0) {
        physicsEviction.values.push_back(elapsed(physicsStart));
        eviction.values.push_back(removalMs);
      }
      if (world.entities.size() != 1 ||
          !physics.overlapSphere({}, float(metres) * 4).empty()) {
        throw std::runtime_error("Cell eviction left scene or physics objects");
      }
    }
    const char *name =
        mode == TerrainVegetationStorage::Stored ? "stored" : "procedural";
    for (auto &[phase, samples] : profileSamples) {
      samples.report(metres, name, phase, count, colliders);
    }
    std::cerr << "Owned cell payload: " << metres << "m " << name << " "
              << peakOwnedPayload << " bytes (accounted payload, not RSS)\n";
    if (framePaced) {
      loadingWork.report(metres, name, "loading_frame_cpu_work", count,
                         colliders);
      loadingIntervals.report(metres, name, "loading_tick_interval", count,
                              colliders);
      std::cerr << "Loading CPU frames above 16.67ms: " << name << ' '
                << loadingOverBudget << '/' << loadingWork.values.size()
                << '\n';
    }
    worker.report(metres, name, "worker_load", count, colliders);
    workerPreparation.report(metres, name, "worker_cell_preparation", count,
                             colliders);
    publication.report(metres, name, "scene_publication", count, colliders);
    physicsArrival.report(metres, name, "physics_arrival", count, colliders);
    combinedArrival.report(metres, name, "main_arrival_combined", count,
                           colliders);
    readiness.report(metres, name, "request_to_physics_ready", count,
                     colliders);
    steady.report(metres, name, "steady_update_and_physics", count, colliders);
    eviction.report(metres, name, "scene_eviction", count, colliders);
    physicsEviction.report(metres, name, "physics_eviction", count, colliders);
  }
}
} // namespace

int main(int argc, char **argv) {
  try {
    const int metres = argc > 1 ? std::stoi(argv[1]) : 128;
    const int repetitions = argc > 2 ? std::stoi(argv[2]) : 5;
    if ((metres != 64 && metres != 128 && metres != 256 && metres != 512) ||
        repetitions < 1 || repetitions > 100) {
      throw std::invalid_argument(
          "Usage: demi-terrain-publication-benchmark "
          "[64|128|256|512] [repetitions 1..100] [visual] [paced]");
    }
    std::cerr << "Release CPU probe: finite generated fixture, visual prefab "
                 "batching + sphere-collider trees.\n"
                 "One warmup per mode, warmed main prefab cache, private "
                 "worker context per load; request-to-ready "
                 "includes polling delay (1ms by default, 60Hz with paced).\n"
                 "Includes World publication and native Jolt synchronization. "
                 "Excludes GPU upload/drawing, mesh asset decoding and terrain "
                 "mesh/collision.\n";
    std::cout << std::fixed << std::setprecision(6)
              << "cell_metres,mode,placements,colliders,phase,samples,median_"
                 "ms,p95_ms,max_ms\n";
    bool visualOnly = false;
    bool framePaced = false;
    for (int index = 3; index < argc; ++index) {
      const std::string_view option = argv[index];
      if (option == "visual") {
        visualOnly = true;
      } else if (option == "paced") {
        framePaced = true;
      } else {
        throw std::invalid_argument("Unknown mode; use visual and/or paced");
      }
    }
    if (framePaced) {
      std::cerr << "60Hz CPU frame-loop probe: update and physics while jobs "
                   "run; no rendered frames.\n";
    }
    if (visualOnly) {
      std::cerr << "Visual-only comparison: all tree collision disabled in "
                   "this probe.\n";
    }
    RuntimeProfiler::setEnabled(true);
    run(metres, repetitions, visualOnly, framePaced);
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
