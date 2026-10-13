#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/terrain/TerrainGenerator.h"
#include "demi/runtime/terrain/TerrainLodResidency.h"

#include <atomic>
#include <cassert>
#include <iostream>
#include <latch>
#include <stdexcept>

using namespace demi::runtime;

namespace {

void finishQueuedWork(JobSystem &jobs, TerrainLodResidency &cache) {
  // Tests use a single worker, whose FIFO fence follows every submitted build.
  // Production presentation code only polls completion; it never uses a fence.
  jobs.submit([] {}).wait();
  cache.pollCompleted();
}

std::shared_ptr<const HeightField> fieldForTest() {
  TerrainRecipe recipe;
  recipe.size = {8, 8};
  recipe.cellsX = 8;
  recipe.cellsZ = 8;
  recipe.chunkCells = 8;
  recipe.landforms.at("default").heightVariation = 0;
  auto field = TerrainGenerator::generate(recipe);
  assert(field);
  return std::make_shared<const HeightField>(std::move(*field));
}

std::shared_ptr<const PreparedTerrainChunkLod> descriptorForTest() {
  const auto field = fieldForTest();
  const auto descriptor = prepareTerrainRenderLod(field, field->chunks.front());
  assert(descriptor);
  return descriptor;
}

void demandIsSingleFlightAndCameraUnionIsRetained() {
  JobSystem jobs(1);
  TerrainLodResidency cache(jobs);
  const auto descriptor = descriptorForTest();
  cache.beginPresentation();
  assert(!cache.request(descriptor, 1));
  const auto sameAppearance =
      std::make_shared<const PreparedTerrainChunkLod>(*descriptor);
  assert(!cache.request(sameAppearance, 1));
  assert(cache.statistics().submittedJobs == 1);
  assert(cache.statistics().pendingJobs == 1);
  assert(cache.statistics().pendingBytes > 0);
  cache.endPresentation();
  finishQueuedWork(jobs, cache);
  assert(cache.statistics().pendingJobs == 0);
  assert(cache.statistics().residentBytes > 0);
  assert(cache.statistics().readyLevels == 1);

  cache.beginPresentation();
  const auto first = cache.request(descriptor, 1);
  assert(first);
  assert(cache.request(sameAppearance, 1) == first);
  assert(!cache.request(descriptor, 2));
  cache.endPresentation();
  finishQueuedWork(jobs, cache);
  assert(cache.statistics().readyLevels == 2);
  assert(cache.statistics().submittedJobs == 2);

  cache.beginPresentation();
  assert(cache.request(descriptor, 1) == first);
  assert(cache.request(descriptor, 2));
  cache.endPresentation();
  assert(cache.statistics().readyLevels == 2);
  assert(cache.statistics().submittedJobs == 2);

  cache.beginPresentation();
  assert(cache.request(descriptor, 1) == first);
  cache.endPresentation();
  assert(cache.statistics().readyLevels == 1);
  cache.tickInactive();
  assert(cache.statistics().readyLevels == 0);
  assert(cache.statistics().residentBytes == 0);
}

void readinessIsFrozenUntilTheNextPresentation() {
  JobSystem jobs(1);
  TerrainLodResidency cache(jobs);
  const auto descriptor = descriptorForTest();
  cache.beginPresentation();
  assert(!cache.request(descriptor, 1));
  finishQueuedWork(jobs, cache);
  // Even an explicit poll cannot change later biome/shadow views in this frame.
  cache.pollCompleted();
  assert(!cache.request(descriptor, 1));
  assert(cache.statistics().readyLevels == 0);
  cache.endPresentation();
  cache.beginPresentation();
  assert(cache.request(descriptor, 1));
  cache.endPresentation();
}

void staleBindingsCannotPublishAfterCancellation() {
  JobSystem jobs(1);
  std::latch started(1);
  std::latch release(1);
  std::atomic<bool> first = true;
  TerrainLodResidency cache(jobs, [&](const PreparedTerrainChunkLod &descriptor,
                                      std::size_t level, std::stop_token stop) {
    if (first.exchange(false)) {
      started.count_down();
      release.wait();
    }
    return prepareTerrainRenderLodLevel(descriptor, level, stop);
  });
  const auto descriptor = descriptorForTest();
  auto changedField = std::make_shared<HeightField>(*descriptor->field);
  changedField->biomeMaterials.assign(changedField->biomeIds.size(),
                                      "asset://materials/new_binding");
  const auto rebound = rebindTerrainRenderLod(changedField, *descriptor);
  assert(rebound);
  cache.beginPresentation();
  assert(!cache.request(descriptor, 1));
  started.wait();
  cache.endPresentation();
  cache.beginPresentation();
  assert(!cache.request(rebound, 1));
  release.count_down();
  cache.endPresentation();
  finishQueuedWork(jobs, cache);
  assert(cache.statistics().discardedJobs == 1);
  assert(cache.statistics().readyLevels == 0);
  cache.beginPresentation();
  assert(!cache.request(rebound, 1));
  cache.endPresentation();
  finishQueuedWork(jobs, cache);
  cache.beginPresentation();
  assert(cache.request(rebound, 1));
  cache.endPresentation();
}

void absentDemandCancelsPendingWithoutWaiting() {
  JobSystem jobs(1);
  std::latch started(1);
  std::latch release(1);
  TerrainLodResidency cache(jobs, [&](const PreparedTerrainChunkLod &descriptor,
                                      std::size_t level, std::stop_token stop) {
    started.count_down();
    release.wait();
    return prepareTerrainRenderLodLevel(descriptor, level, stop);
  });
  const auto descriptor = descriptorForTest();
  cache.beginPresentation();
  assert(!cache.request(descriptor, 1));
  cache.endPresentation();
  started.wait();
  // The worker is blocked by the test, so returning here proves retirement
  // does not wait for it. Its reserved bytes remain charged until completion.
  cache.tickInactive();
  assert(cache.statistics().pendingJobs == 1);
  assert(cache.statistics().readyLevels == 0);
  release.count_down();
  finishQueuedWork(jobs, cache);
  assert(cache.statistics().pendingJobs == 0);
  assert(cache.statistics().pendingBytes == 0);
  assert(cache.statistics().discardedJobs == 1);
}

void bindingChangesReuseGeometryAndProvenance() {
  JobSystem jobs(1);
  TerrainLodResidency cache(jobs);
  const auto descriptor = descriptorForTest();
  cache.beginPresentation();
  assert(!cache.request(descriptor, 1));
  cache.endPresentation();
  finishQueuedWork(jobs, cache);
  cache.beginPresentation();
  const auto before = cache.request(descriptor, 1);
  assert(before && !before->empty());
  cache.endPresentation();

  auto changedField = std::make_shared<HeightField>(*descriptor->field);
  changedField->biomeMaterials.assign(changedField->biomeIds.size(),
                                      "asset://materials/rebound");
  const auto rebound = rebindTerrainRenderLod(changedField, *descriptor);
  assert(rebound && rebound->geometryIdentity == descriptor->geometryIdentity);
  // This leaves room for section rebinding, but not a fictitious second copy
  // of all reduced vertices and provenance.
  cache.setSettings(
      {.maximumResidentBytes = cache.statistics().residentBytes + 4096});
  cache.beginPresentation();
  assert(!cache.request(rebound, 1));
  cache.endPresentation();
  // The rebind job retains and accounts for the old immutable geometry.
  assert(cache.statistics().residentBytes > 0);
  assert(cache.statistics().pendingJobs == 1);
  finishQueuedWork(jobs, cache);
  cache.beginPresentation();
  const auto after = cache.request(rebound, 1);
  assert(after && after != before);
  assert(after->size() == before->size());
  for (const auto &[biome, surface] : *after) {
    const auto &old = before->at(biome);
    assert(surface.geometry == old.geometry);
    assert(surface.sampleIndices == old.sampleIndices);
  }
  cache.endPresentation();
  assert(cache.statistics().readyLevels == 1);
}

void workAndByteBudgetsKeepFullFallback() {
  JobSystem jobs(1);
  TerrainLodResidency cache(jobs);
  const auto descriptor = descriptorForTest();
  cache.setSettings({.maximumResidentBytes = 1});
  cache.beginPresentation();
  assert(!cache.request(descriptor, 1));
  cache.endPresentation();
  assert(cache.statistics().submittedJobs == 0);
  assert(cache.statistics().budgetPressure > 0);

  cache.setSettings({.maximumJobsPerPresentation = 1});
  cache.beginPresentation();
  assert(!cache.request(descriptor, 1));
  assert(!cache.request(descriptor, 2));
  cache.endPresentation();
  assert(cache.statistics().submittedJobs == 1);
  finishQueuedWork(jobs, cache);
  assert(cache.statistics().readyLevels == 1);
  cache.setSettings({.maximumResidentBytes = 0});
  assert(cache.statistics().readyLevels == 0);
  assert(cache.statistics().residentBytes == 0);

  JobSystem inlineJobs(0);
  std::atomic<bool> called = false;
  TerrainLodResidency inlineCache(
      inlineJobs,
      [&](const PreparedTerrainChunkLod &, std::size_t, std::stop_token) {
        called = true;
        return std::shared_ptr<const PreparedTerrainLodLevel>{};
      });
  inlineCache.beginPresentation();
  assert(!inlineCache.request(descriptor, 1));
  inlineCache.endPresentation();
  assert(!called);
  assert(inlineCache.statistics().submittedJobs == 0);
}

void materialAppearancesCoexistWithoutGeometryDuplication() {
  JobSystem jobs(1);
  TerrainLodResidency cache(jobs);
  const auto original = descriptorForTest();
  auto field = std::make_shared<HeightField>(*original->field);
  field->biomeMaterials.assign(field->biomeIds.size(),
                               "asset://material/other");
  const auto other = rebindTerrainRenderLod(field, *original);
  cache.beginPresentation();
  assert(!cache.request(original, 1));
  assert(!cache.request(other, 1));
  cache.endPresentation();
  finishQueuedWork(jobs, cache);
  assert(cache.statistics().submittedJobs == 1);
  cache.beginPresentation();
  const auto first = cache.request(original, 1);
  assert(first);
  assert(!cache.request(other, 1));
  cache.endPresentation();
  finishQueuedWork(jobs, cache);
  cache.beginPresentation();
  const auto second = cache.request(other, 1);
  assert(second);
  assert(cache.request(original, 1) == first);
  assert(first->begin()->second.geometry == second->begin()->second.geometry);
  cache.endPresentation();
  assert(cache.statistics().readyLevels == 2);
  assert(cache.statistics().submittedJobs == 2);
}

void oversizedCompletionIsTerminalUntilSettingsChange() {
  JobSystem jobs(1);
  TerrainLodResidency cache(
      jobs, [](const PreparedTerrainChunkLod &, std::size_t, std::stop_token) {
        auto mesh = std::make_shared<MeshRendererComponent>();
        mesh->vertices.resize(100000);
        auto level = std::make_shared<PreparedTerrainLodLevel>();
        (*level)["default"].geometry = std::move(mesh);
        return std::shared_ptr<const PreparedTerrainLodLevel>(std::move(level));
      });
  auto descriptor =
      std::make_shared<PreparedTerrainChunkLod>(*descriptorForTest());
  // An injected builder exceeds the reservation on purpose; production must
  // still reject publication and avoid resubmitting the same failed work.
  descriptor->triangleCounts[1] = 0;
  cache.setSettings({.maximumResidentBytes = 8192});
  cache.beginPresentation();
  assert(!cache.request(descriptor, 1));
  cache.endPresentation();
  finishQueuedWork(jobs, cache);
  assert(cache.statistics().submittedJobs == 1);
  assert(cache.statistics().discardedJobs == 1);
  assert(cache.statistics().residentBytes == 0);
  cache.beginPresentation();
  assert(!cache.request(descriptor, 1));
  cache.endPresentation();
  assert(cache.statistics().submittedJobs == 1);
  assert(!cache.hasPendingWork());
  cache.setSettings({.maximumResidentBytes = 4U * 1024U * 1024U});
  cache.beginPresentation();
  assert(!cache.request(descriptor, 1));
  cache.endPresentation();
  finishQueuedWork(jobs, cache);
  cache.beginPresentation();
  assert(cache.request(descriptor, 1));
  cache.endPresentation();
}

void failedPreparationLeavesNoPartialReadyLevel() {
  JobSystem jobs(1);
  TerrainLodResidency cache(
      jobs,
      [](const PreparedTerrainChunkLod &, std::size_t,
         std::stop_token) -> std::shared_ptr<const PreparedTerrainLodLevel> {
        throw std::runtime_error("injected preparation failure");
      });
  const auto descriptor = descriptorForTest();
  cache.beginPresentation();
  assert(!cache.request(descriptor, 1));
  cache.endPresentation();
  finishQueuedWork(jobs, cache);
  assert(cache.statistics().failedJobs == 1);
  assert(cache.statistics().residentBytes == 0);
  assert(cache.statistics().pendingJobs == 0);
  assert(cache.lastError() == "injected preparation failure");
  for (int presentation = 0; presentation < 3; ++presentation) {
    cache.beginPresentation();
    assert(!cache.request(descriptor, 1));
    cache.endPresentation();
    assert(!cache.hasPendingWork());
  }
  assert(cache.statistics().submittedJobs == 1);
}

} // namespace

int main() {
  demandIsSingleFlightAndCameraUnionIsRetained();
  readinessIsFrozenUntilTheNextPresentation();
  staleBindingsCannotPublishAfterCancellation();
  absentDemandCancelsPendingWithoutWaiting();
  bindingChangesReuseGeometryAndProvenance();
  workAndByteBudgetsKeepFullFallback();
  failedPreparationLeavesNoPartialReadyLevel();
  materialAppearancesCoexistWithoutGeometryDuplication();
  oversizedCompletionIsTerminalUntilSettingsChange();
  std::cout << "Terrain LOD residency checks passed\n";
}
