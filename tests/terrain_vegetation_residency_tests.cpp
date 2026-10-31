#include "demi/assets/TerrainPlacementCodec.h"
#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/terrain/PreparedTerrainCell.h"
#include "demi/runtime/terrain/TerrainVegetationResidency.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace demi::runtime;
using namespace std::chrono_literals;
using Residency = TerrainVegetationResidency;

namespace {
template <typename Predicate> void until(Predicate predicate) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      throw std::runtime_error("Vegetation residency test timed out");
    }
    std::this_thread::sleep_for(1ms);
  }
}

TerrainScatterResult onePlacement(float height = 0) {
  TerrainScatterResult result;
  TerrainScatterPlacement placement;
  placement.ruleId = "trees";
  placement.model = "asset://test/tree";
  placement.position.y = height;
  result.placements.push_back(std::move(placement));
  return result;
}

void storedAndProceduralLifecycle() {
  TerrainRecipe recipe;
  recipe.size = {32, 32};
  recipe.cellsX = recipe.cellsZ = 16;
  auto field = *TerrainGenerator::generate(recipe);
  TerrainPalette palette;
  TerrainPaletteEntry grass;
  grass.model = "asset://test/grass";
  grass.density = 0.6F;
  grass.candidateSpacing = 1;
  grass.spacing = 0;
  grass.positionJitter = 0.8F;
  grass.clusterStrength = 0.7F;
  palette.placements.emplace("grass", grass);
  auto expected = scatterTerrain(field, recipe, palette);
  assert(!expected.placements.empty());
  field.scatterPlacements = expected.placements;
  const auto packed = demi::assets::terrain_payload::encodePlacements(field);
  field.scatterPlacements.clear();
  field.scatterPlacements.shrink_to_fit();

  JobSystem jobs(1);
  std::atomic<int> generated = 0;
  std::atomic<int> decoded = 0;
  Residency cells(
      jobs, {.maximumBytes = 4 * 1024 * 1024},
      [&](const auto &key, std::stop_token stop) {
        if (key.storage == TerrainVegetationStorage::Procedural) {
          ++generated;
          return scatterTerrain(field, recipe, palette, {}, nullptr, {}, stop);
        }
        ++decoded;
        auto loaded = field;
        demi::assets::terrain_payload::decodePlacements(packed, loaded);
        return TerrainScatterResult{std::move(loaded.scatterPlacements), false};
      });
  Residency::Demand request{{-4, 7, 1, TerrainVegetationStorage::Procedural},
                            2 * 1024 * 1024};
  for (int visit = 0; visit < 3; ++visit) {
    until([&] {
      cells.update(std::span(&request, 1));
      return cells.find(request.key) != nullptr;
    });
    assert(generated == visit + 1);
    const auto *resident = cells.find(request.key);
    auto reconstructed = field;
    reconstructed.scatterPlacements = *resident;
    // Exact packed floats, identities and shared definitions match stored mode.
    assert(demi::assets::terrain_payload::encodePlacements(reconstructed) ==
           packed);
    for (int frame = 0; frame < 100; ++frame) {
      cells.update(std::span(&request, 1));
      assert(cells.find(request.key) == resident);
    }
    assert(generated == visit + 1);
    assert(cells.statistics().residentCells == 1);
    cells.clear();
    assert(cells.find(request.key) == nullptr);
    assert(cells.statistics().residentBytes == 0);
  }
  request.key.storage = TerrainVegetationStorage::Stored;
  until([&] {
    cells.update(std::span(&request, 1));
    return cells.find(request.key) != nullptr;
  });
  auto restored = field;
  restored.scatterPlacements = *cells.find(request.key);
  assert(demi::assets::terrain_payload::encodePlacements(restored) == packed);
  assert(decoded == 1);
  assert(generated == 3);
}

void cancellationAndRevision() {
  JobSystem jobs(1);
  std::atomic<bool> started = false;
  std::atomic<bool> release = false;
  Residency cells(jobs, {.maximumBytes = 4096, .maximumPendingJobs = 1},
                  [&](const auto &key, std::stop_token) {
                    if (key.revision == 1) {
                      started = true;
                      // Simulate a decoder which cannot immediately cancel. Its
                      // reservation must stay charged and its result must never
                      // replace revision two.
                      until([&] { return release.load(); });
                    }
                    return onePlacement(float(key.revision));
                  });
  Residency::Demand old{{1, 2, 1}, 4096};
  cells.update(std::span(&old, 1));
  until([&] { return started.load(); });
  auto next = old;
  next.key.revision = 2;
  cells.update(std::span(&next, 1));
  assert(cells.statistics().pendingBytes == 4096);
  assert(cells.statistics().submitted == 1);
  assert(cells.find(old.key) == nullptr);
  release = true;
  until([&] {
    cells.update(std::span(&next, 1));
    return cells.find(next.key) != nullptr;
  });
  assert(cells.statistics().discarded == 1);
  assert(cells.statistics().published == 1);
  assert(cells.find(next.key)->front().position.y == 2);
  assert(cells.find(old.key) == nullptr);
  cells.clear();
  assert(cells.statistics().residentBytes == 0);
  assert(cells.statistics().pendingBytes == 0);
  until([&] {
    cells.clear();
    return cells.statistics().retiringJobs == 0;
  });
  assert(cells.statistics().retiringBytes == 0);
}

void cancelledCellRevisitedBeforeCompletion() {
  JobSystem jobs(1);
  std::atomic<int> loads = 0;
  std::atomic<bool> release = false;
  Residency cells(jobs, {.maximumBytes = 4096, .maximumPendingJobs = 1},
                  [&](const auto &, std::stop_token) {
                    const int load = ++loads;
                    if (load == 1) {
                      until([&] { return release.load(); });
                    }
                    return onePlacement(float(load));
                  });
  Residency::Demand request{{-3, -8, 1}, 4096};
  cells.update(std::span(&request, 1));
  until([&] { return loads == 1; });
  cells.clear();
  assert(cells.statistics().pendingBytes == 4096);
  cells.update(std::span(&request, 1));
  assert(loads == 1);
  assert(cells.find(request.key) == nullptr);
  release = true;
  until([&] {
    cells.update(std::span(&request, 1));
    return cells.find(request.key) != nullptr;
  });
  assert(loads == 2);
  assert(cells.statistics().discarded == 1);
  assert(cells.find(request.key)->front().position.y == 2);
  cells.clear();
  until([&] {
    cells.clear();
    return cells.statistics().retiringJobs == 0;
  });
  assert(cells.statistics().residentBytes == 0);
  assert(cells.statistics().pendingBytes == 0);
  assert(cells.statistics().retiringBytes == 0);
}

void failuresAndBudgets() {
  JobSystem jobs(1);
  std::atomic<int> calls = 0;
  Residency cells(
      jobs, {.maximumBytes = 4096}, [&](const auto &key, std::stop_token) {
        ++calls;
        if (key.revision == 1) {
          throw std::runtime_error("Missing stored vegetation cell");
        }
        auto result = onePlacement();
        result.truncated = key.revision == 2;
        return result;
      });
  Residency::Demand request{{0, 0, 1}, 8192};
  cells.update(std::span(&request, 1));
  assert(calls == 0);
  assert(cells.statistics().budgetMisses == 1);
  request.preparationBytes = 4096;
  until([&] {
    cells.update(std::span(&request, 1));
    return !cells.error(request.key).empty();
  });
  assert(cells.error(request.key) == "Missing stored vegetation cell");
  for (int frame = 0; frame < 100; ++frame) {
    cells.update(std::span(&request, 1));
  }
  assert(calls == 1);
  assert(cells.statistics().pendingBytes == 0);
  request.key.revision = 2;
  until([&] {
    cells.update(std::span(&request, 1));
    return !cells.error(request.key).empty();
  });
  assert(cells.error(request.key).find("truncated") != std::string::npos);
  request.key.revision = 3;
  request.preparationBytes = 1;
  until([&] {
    cells.update(std::span(&request, 1));
    return !cells.error(request.key).empty();
  });
  assert(cells.error(request.key).find("reservation") != std::string::npos);
  assert(cells.statistics().residentCells == 0);
  const auto before = cells.statistics();
  const std::vector duplicates{request, request};
  bool rejected = false;
  try {
    cells.update(duplicates);
  } catch (const std::invalid_argument &) {
    rejected = true;
  }
  assert(rejected);
  assert(cells.statistics().submitted == before.submitted);
}

void publicationAndDestruction() {
  JobSystem jobs(2);
  std::atomic<int> finished = 0;
  {
    Residency cells(jobs,
                    {.maximumBytes = 8192,
                     .maximumPendingJobs = 2,
                     .maximumStartsPerUpdate = 2,
                     .maximumPublicationsPerUpdate = 1},
                    [&](const auto &, std::stop_token) {
                      ++finished;
                      return onePlacement();
                    });
    const std::vector<Residency::Demand> demand{{{0, 0, 1}, 4096},
                                                {{1, 0, 1}, 4096}};
    cells.update(demand);
    until([&] { return finished == 2; });
    until([&] {
      const auto previous = cells.statistics().published;
      cells.update(demand);
      assert(cells.statistics().published - previous <= 1);
      assert(cells.statistics().residentBytes +
                 cells.statistics().pendingBytes +
                 cells.statistics().retiringBytes <=
             8192);
      return cells.statistics().residentCells == 2;
    });
  }
  std::atomic<bool> entered = false;
  std::atomic<bool> cancelled = false;
  {
    Residency cells(jobs, {.maximumBytes = 4096},
                    [&](const auto &, std::stop_token stop) {
                      entered = true;
                      until([&] { return stop.stop_requested(); });
                      cancelled = true;
                      return onePlacement();
                    });
    Residency::Demand demand{{0, 0, 1}, 4096};
    cells.update(std::span(&demand, 1));
    until([&] { return entered.load(); });
  }
  assert(cancelled);
}
void preparationAccountingAndLifetime() {
  JobSystem jobs(1);
  const auto mainThread = std::this_thread::get_id();
  std::weak_ptr<const PreparedTerrainCell> observed;
  Residency cells(
      jobs, {.maximumBytes = 8192},
      [](const auto &, std::stop_token) { return onePlacement(); },
      [&](const auto &, auto &placements, std::stop_token) {
        assert(std::this_thread::get_id() != mainThread);
        auto visuals = std::make_shared<PreparedTerrainCell>();
        visuals->accountedBytes = 4096;
        placements.clear();
        placements.shrink_to_fit();
        observed = visuals;
        return visuals;
      });
  Residency::Demand request{{0, 0, 1}, 8192};
  until([&] {
    cells.update(std::span(&request, 1));
    return cells.find(request.key) != nullptr;
  });
  assert(cells.find(request.key)->empty());
  assert(cells.prepared(request.key));
  assert(cells.statistics().residentBytes >= 4096);
  cells.clear();
  until([&] {
    cells.clear();
    return cells.statistics().retiringJobs == 0;
  });
  assert(observed.expired());
  assert(cells.statistics().retiringBytes == 0);
  request.key.revision = 2;
  request.preparationBytes = 2048;
  until([&] {
    cells.update(std::span(&request, 1));
    return !cells.error(request.key).empty();
  });
  assert(!cells.find(request.key));
  assert(cells.error(request.key).find("reservation") != std::string::npos);
}
} // namespace

int main() {
  preparationAccountingAndLifetime();
  storedAndProceduralLifecycle();
  cancellationAndRevision();
  cancelledCellRevisitedBeforeCompletion();
  failuresAndBudgets();
  publicationAndDestruction();
  std::cout << "Terrain vegetation residency tests passed\n";
}
