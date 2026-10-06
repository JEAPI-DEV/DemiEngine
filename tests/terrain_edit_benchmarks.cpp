#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainGenerationCache.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainUpdate.h"
#include "demi/runtime/terrain/TerrainWorld.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

namespace {
std::atomic<bool> countAllocations = false;
std::atomic<std::size_t> allocations = 0, allocatedBytes = 0;
void beginAllocations() {
  allocations = 0;
  allocatedBytes = 0;
  countAllocations = true;
}
} // namespace
void *operator new(std::size_t bytes) {
  if (void *memory = std::malloc(bytes ? bytes : 1)) {
    if (countAllocations.load(std::memory_order_relaxed)) {
      allocations.fetch_add(1, std::memory_order_relaxed);
      allocatedBytes.fetch_add(bytes, std::memory_order_relaxed);
    }
    return memory;
  }
  throw std::bad_alloc();
}
void operator delete(void *memory) noexcept { std::free(memory); }
void operator delete(void *memory, std::size_t) noexcept { std::free(memory); }
void *operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete[](void *memory) noexcept { std::free(memory); }
void operator delete[](void *memory, std::size_t) noexcept {
  std::free(memory);
}

using namespace demi::runtime;
using Clock = std::chrono::steady_clock;

int main() {
  for (const int cells : {256, 512}) {
    std::vector<double> generationTimes, publicationTimes;
    std::vector<std::size_t> fullAllocations, fullBytes;
    for (int iteration = 0; iteration < 5; ++iteration) {
      TerrainRecipe recipe;
      recipe.size = {float(cells), float(cells)};
      recipe.cellsX = recipe.cellsZ = cells;
      TerrainEdit edit;
      edit.center = {cells * .5F + .25F, cells * .5F + .25F};
      edit.radius = 3;
      edit.amount = 1;
      recipe.edits.push_back(edit);
      beginAllocations();
      const auto start = Clock::now();
      auto generated = TerrainGenerator::generate(recipe);
      auto field = std::make_shared<const HeightField>(std::move(*generated));
      const auto sampled = Clock::now();
      const auto json = recipe.toJson();
      // Keyed on the same fingerprint scene loading would supply, so the
      // benchmark measures the same cache path the engine uses.
      publishTerrain(json, field->inputFingerprint, field);
      World world;
      Entity owner;
      owner.id = "terrain";
      owner.setComponent(Transform3DComponent{});
      Terrain3DComponent terrain;
      terrain.recipe = json;
      owner.setComponent(std::move(terrain));
      world.entities.push_back(std::move(owner));
      std::string error;
      if (!materializeTerrains(world, error))
        throw std::runtime_error(error);
      const auto published = Clock::now();
      countAllocations = false;
      fullAllocations.push_back(allocations);
      fullBytes.push_back(allocatedBytes);
      generationTimes.push_back(
          std::chrono::duration<double, std::milli>(sampled - start).count());
      publicationTimes.push_back(
          std::chrono::duration<double, std::milli>(published - sampled)
              .count());
    }
    std::ranges::sort(generationTimes);
    std::ranges::sort(publicationTimes);
    std::ranges::sort(fullAllocations);
    std::ranges::sort(fullBytes);
    std::cout << "cells=" << cells
              << " full_generation_ms=" << generationTimes[2]
              << " full_publication_ms=" << publicationTimes[2]
              << " full_allocations=" << fullAllocations[2]
              << " full_requested_bytes=" << fullBytes[2] << '\n';

    std::vector<double> updateTimes, publishTimes;
    std::vector<std::size_t> localAllocations, localBytes;
    for (int iteration = 0; iteration < 5; ++iteration) {
      TerrainRecipe before;
      before.size = {float(cells), float(cells)};
      before.cellsX = before.cellsZ = cells;
      World world;
      Entity owner;
      owner.id = "terrain";
      owner.setComponent(Transform3DComponent{});
      Terrain3DComponent terrain;
      terrain.recipe = before.toJson();
      owner.setComponent(std::move(terrain));
      world.entities.push_back(std::move(owner));
      std::string error;
      if (!materializeTerrains(world, error))
        throw std::runtime_error(error);
      const auto previous =
          world.entities.front().component<Terrain3DComponent>()->generated;
      auto after = before;
      TerrainEdit edit;
      edit.center = {cells * .5F + .25F, cells * .5F + .25F};
      edit.radius = 3;
      edit.amount = 1;
      after.edits.push_back(edit);
      beginAllocations();
      const auto start = Clock::now();
      const auto update = updateTerrain(before, after, previous);
      const auto sampled = Clock::now();
      if (!updateTerrainWorld(world, "terrain", after.toJson(), *update, error))
        throw std::runtime_error(error);
      const auto published = Clock::now();
      countAllocations = false;
      localAllocations.push_back(allocations);
      localBytes.push_back(allocatedBytes);
      updateTimes.push_back(
          std::chrono::duration<double, std::milli>(sampled - start).count());
      publishTimes.push_back(
          std::chrono::duration<double, std::milli>(published - sampled)
              .count());
      if (iteration == 0)
        std::cout << "cells=" << cells
                  << " base_evaluations=" << update->stats.baseEvaluations
                  << " edit_evaluations=" << update->stats.editEvaluations
                  << " changed_samples=" << update->stats.changedSamples
                  << " local_history_bytes=" << update->patch->retainedBytes()
                  << '\n';
    }
    std::ranges::sort(updateTimes);
    std::ranges::sort(publishTimes);
    std::ranges::sort(localAllocations);
    std::ranges::sort(localBytes);
    std::cout << "cells=" << cells
              << " incremental_update_ms=" << updateTimes[2]
              << " incremental_publication_ms=" << publishTimes[2]
              << " local_allocations=" << localAllocations[2]
              << " local_requested_bytes=" << localBytes[2] << '\n';
  }
}
