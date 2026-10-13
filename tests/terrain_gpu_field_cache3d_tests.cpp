#include "demi/runtime/render/bgfx3d/TerrainGpuFieldCache3D.h"
#include "demi/runtime/terrain/TerrainSurfaceMaterials.h"

#include <cassert>
#include <cstring>
#include <iostream>
#include <latch>
#include <map>
#include <set>
#include <stdexcept>
#include <thread>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

class TestResources final : public GpuResources {
public:
  struct Texture {
    TextureFormat format;
    std::vector<std::byte> bytes;
  };
  RenderDeviceLimits capabilities{.maxTextureSize = 64,
                                  .maxTextureSamplers = 7,
                                  .supportsInstancing = true};
  bool vertexSampling = true;
  std::size_t createCalls = 0;
  std::size_t failCreateAt = 0;
  std::map<std::uint32_t, Texture> textures;
  std::set<std::uint32_t> destroyed;
  const std::thread::id owner = std::this_thread::get_id();

  RenderDeviceLimits limits() const override { return capabilities; }
  TextureCapabilities textureCapabilities(TextureFormat) const override {
    return {.sampled2D = true, .vertexSampling = vertexSampling};
  }
  TextureHandle createTexture(const TextureCreateInfo &info,
                              std::string &error) override {
    assert(std::this_thread::get_id() == owner);
    ++createCalls;
    if (createCalls == failCreateAt) {
      error = "Injected field texture allocation failure";
      return {};
    }
    assert(info.filter == TextureFilter::Nearest &&
           info.wrap == TextureWrap::Clamp);
    const auto id = static_cast<std::uint32_t>(createCalls);
    textures.emplace(
        id, Texture{info.format, {info.data.begin(), info.data.end()}});
    return {.index = id, .generation = 1};
  }
  bool destroy(TextureHandle handle) override {
    assert(std::this_thread::get_id() == owner);
    assert(destroyed.insert(handle.index).second);
    return textures.erase(handle.index) == 1;
  }
  SamplerHandle createSampler(std::string_view, std::string &) override {
    return {};
  }
  UniformHandle createUniform(std::string_view, UniformType, std::uint16_t,
                              std::string &) override {
    return {};
  }
  BufferHandle createBuffer(const BufferCreateInfo &, std::string &) override {
    return {};
  }
  ProgramHandle createProgram(const ProgramCreateInfo &,
                              std::string &) override {
    return {};
  }
  ProgramHandle createBuiltinProgram(BuiltinProgram, std::string &) override {
    return {};
  }
  std::string_view shaderBackend() const override { return "test"; }
  RenderTargetHandles createRenderTarget(const RenderTargetCreateInfo &,
                                         std::string &) override {
    return {};
  }
  bool destroy(SamplerHandle) override { return false; }
  bool destroy(UniformHandle) override { return false; }
  bool destroy(BufferHandle) override { return false; }
  bool destroy(ProgramHandle) override { return false; }
  bool destroy(FrameBufferHandle) override { return false; }
  void clear() override { assert(textures.empty()); }
};

std::shared_ptr<const HeightField> fieldForTest() {
  auto field = std::make_shared<HeightField>();
  field->cellsX = field->cellsZ = 2;
  field->size = {4, 4};
  field->heights = {0, 1, 2, 3, 4, 5, 6, 7, 8};
  field->normals.assign(9, {0, 1, 0});
  field->biomeIndices.assign(9, 0);
  field->biomeIds = {"ground"};
  field->biomeColors = {{0.2F, 0.4F, 0.6F, 1}};
  return field;
}

void finish(JobSystem &jobs, TerrainGpuFieldCache3D &cache) {
  jobs.submit([] {}).wait();
  cache.pollCompleted();
}

std::shared_ptr<const TerrainGpuField3D>
prepareReady(JobSystem &jobs, TerrainGpuFieldCache3D &cache,
             const std::shared_ptr<const HeightField> &source,
             const TerrainGpuMaterialResolver3D &resolver = {}) {
  cache.beginPresentation();
  assert(cache.request(source, resolver).preparing);
  cache.endPresentation();
  finish(jobs, cache);
  cache.beginPresentation();
  const auto ready = cache.request(source, resolver).field;
  assert(ready);
  cache.endPresentation();
  return ready;
}

void singleFlightReadinessAndBorrowedLifetime() {
  TestResources resources;
  JobSystem jobs(1);
  TerrainGpuFieldCache3D cache(resources, jobs);
  const auto source = fieldForTest();
  cache.beginPresentation();
  assert(cache.request(source, TerrainGpuMaterialResolver3D{}).preparing);
  assert(cache.request(source, TerrainGpuMaterialResolver3D{}).preparing);
  assert(cache.pendingJobs() == 1 && cache.pendingBytes() > 0);
  finish(jobs, cache);
  assert(resources.createCalls == 0);
  assert(cache.request(source, TerrainGpuMaterialResolver3D{}).preparing);
  cache.endPresentation();
  cache.pollCompleted();
  assert(resources.createCalls == 4);
  assert(cache.pendingJobs() == 0 && cache.pendingBytes() == 0);
  assert(cache.residentBytes() == 9 * 24 + 32);
  cache.beginPresentation();
  auto ready = cache.request(source, TerrainGpuMaterialResolver3D{}).field;
  assert(ready && ready->source == source);
  assert(ready->fieldParameters[0] == 3 && ready->fieldParameters[2] == 2);
  assert(ready->paletteParameters[2] == 1);
  cache.endPresentation();
  cache.clear();
  assert(resources.textures.size() == 4);
  assert(cache.residentBytes() == 9 * 24 + 32);
  ready.reset();
  assert(resources.textures.empty() && cache.residentBytes() == 0);
}

void appearanceChangesReuseExactChannels() {
  TestResources resources;
  JobSystem jobs(1);
  TerrainGpuFieldCache3D cache(resources, jobs);
  const auto original = fieldForTest();
  auto before = prepareReady(jobs, cache, original);
  const auto oldHeight = before->height;
  const auto oldNormal = before->normal;
  const auto oldIds = before->classification;
  const auto oldPalette = before->palette;
  auto appearance = std::make_shared<HeightField>(*original);
  appearance->biomeColors[0].r = 0.7F;
  const auto after = prepareReady(jobs, cache, appearance);
  assert(after->height == oldHeight && after->normal == oldNormal);
  assert(after->classification == oldIds && after->palette != oldPalette);
  assert(resources.createCalls == 5);
  before.reset();
  assert(resources.textures.size() == 4);
}

void paletteDedupChangesRebuildClassification() {
  TestResources resources;
  JobSystem jobs(1);
  TerrainGpuFieldCache3D cache(resources, jobs);
  auto source = std::make_shared<HeightField>(*fieldForTest());
  source->biomeIds.push_back("other");
  source->biomeColors.push_back({0.8F, 0.7F, 0.6F, 1});
  source->biomeIndices.set(1, 1);
  const auto before = prepareReady(jobs, cache, source);
  auto deduplicated = std::make_shared<HeightField>(*source);
  deduplicated->biomeColors[1] = deduplicated->biomeColors[0];
  const auto after = prepareReady(jobs, cache, deduplicated);
  assert(after->height == before->height && after->normal == before->normal);
  assert(after->classification != before->classification);
  assert(after->paletteParameters[2] == 1);
}

void equalSampleCountsCannotReuseDifferentTextureDimensions() {
  TestResources resources;
  JobSystem jobs(1);
  TerrainGpuFieldCache3D cache(resources, jobs);
  auto source = std::make_shared<HeightField>(*fieldForTest());
  source->cellsZ = 3;
  source->heights.resize(12);
  source->normals.assign(12, {0, 1, 0});
  source->biomeIndices.assign(12, 0);
  const auto before = prepareReady(jobs, cache, source);
  auto reshaped = std::make_shared<HeightField>(*source);
  reshaped->cellsX = 3;
  reshaped->cellsZ = 2;
  const auto after = prepareReady(jobs, cache, reshaped);
  assert(after->height != before->height);
  assert(after->normal != before->normal);
  assert(after->classification != before->classification);
  assert(after->fieldParameters[0] == 4 && after->fieldParameters[1] == 3);
}

void snapshotFailureLeavesNoDanglingPendingEntry() {
  TestResources resources;
  JobSystem jobs(1);
  TerrainGpuFieldCache3D cache(resources, jobs);
  auto source = std::make_shared<HeightField>(*fieldForTest());
  source->biomeMaterials = {"asset://material"};
  cache.beginPresentation();
  const auto failure =
      cache.request(source, [](std::string_view) -> const MaterialBinding * {
        throw std::runtime_error("Injected material snapshot failure");
      });
  assert(!failure.preparing && !failure.field &&
         !failure.fallbackReason.empty());
  assert(cache.pendingJobs() == 0 && cache.pendingBytes() == 0);
  cache.endPresentation();
  cache.clear();
  source->biomeMaterials.clear();
  assert(prepareReady(jobs, cache, source));
}

void allocationFailureLeavesOldBundleAndCleansNewHandles() {
  TestResources resources;
  JobSystem jobs(1);
  TerrainGpuFieldCache3D cache(resources, jobs);
  const auto original = fieldForTest();
  const auto before = prepareReady(jobs, cache, original);
  auto changed = std::make_shared<HeightField>(*original);
  changed->heights.set(0, 20);
  changed->normals.set(0, {0.6F, 0.8F, 0});
  resources.failCreateAt = resources.createCalls + 2;
  cache.beginPresentation();
  cache.touch(original);
  assert(cache.request(changed, TerrainGpuMaterialResolver3D{}).preparing);
  cache.endPresentation();
  finish(jobs, cache);
  cache.beginPresentation();
  const auto failure = cache.request(changed, TerrainGpuMaterialResolver3D{});
  assert(!failure.field && !failure.preparing &&
         !failure.fallbackReason.empty());
  assert(cache.request(original, TerrainGpuMaterialResolver3D{}).field ==
         before);
  cache.endPresentation();
  assert(resources.textures.size() == 4);
  assert(resources.destroyed.size() == 1);
}

void capabilitiesZeroWorkersAndDisabledBudgetsFallBack() {
  TestResources resources;
  JobSystem noWorkers(0);
  TerrainGpuFieldCache3D synchronous(resources, noWorkers);
  const auto source = fieldForTest();
  const auto unavailable =
      synchronous.request(source, TerrainGpuMaterialResolver3D{});
  assert(!unavailable.preparing &&
         unavailable.fallbackReason.find("worker") != std::string::npos);
  assert(resources.createCalls == 0 && synchronous.pendingJobs() == 0);
  JobSystem jobs(1);
  TerrainGpuFieldCache3D cache(resources, jobs);
  resources.vertexSampling = false;
  assert(!cache.request(source, TerrainGpuMaterialResolver3D{})
              .fallbackReason.empty());
  resources.vertexSampling = true;
  resources.capabilities.maxTextureSamplers = 6;
  assert(!cache.request(source, TerrainGpuMaterialResolver3D{})
              .fallbackReason.empty());
  resources.capabilities.maxTextureSamplers = 7;
  cache.setBudgets(0, 1024 * 1024, 2);
  assert(!cache.request(source, TerrainGpuMaterialResolver3D{})
              .fallbackReason.empty());
  assert(cache.pendingJobs() == 0 && resources.createCalls == 0);
}

void zeroPreparationBudgetsRetainValidReadyFields() {
  TestResources resources;
  JobSystem jobs(1);
  TerrainGpuFieldCache3D cache(resources, jobs);
  const auto source = fieldForTest();
  const auto ready = prepareReady(jobs, cache, source);
  const auto uploads = resources.createCalls;
  constexpr std::size_t GpuBudget = 1024 * 1024;
  cache.setBudgets(GpuBudget, GpuBudget, 0);
  cache.beginPresentation();
  const auto retained = cache.request(source, TerrainGpuMaterialResolver3D{});
  assert(retained.field == ready && !retained.preparing);
  auto changed = std::make_shared<HeightField>(*source);
  changed->heights.set(0, 12);
  const auto disabled = cache.request(changed, TerrainGpuMaterialResolver3D{});
  assert(!disabled.field && !disabled.preparing &&
         !disabled.fallbackReason.empty());
  cache.endPresentation();
  assert(cache.pendingJobs() == 0 && resources.createCalls == uploads);

  cache.setBudgets(GpuBudget, 0, 2);
  cache.beginPresentation();
  assert(cache.request(source, TerrainGpuMaterialResolver3D{}).field == ready);
  assert(!cache.request(changed, TerrainGpuMaterialResolver3D{}).preparing);
  cache.endPresentation();
  assert(cache.pendingJobs() == 0 && !cache.hasPendingWork());

  // No worker is needed to draw an already prepared immutable bundle.
  jobs.shutdown();
  cache.setBudgets(GpuBudget, GpuBudget, 2);
  cache.beginPresentation();
  assert(cache.request(source, TerrainGpuMaterialResolver3D{}).field == ready);
  cache.endPresentation();
  assert(resources.createCalls == uploads);

  // Pausing new work must never expose material-invalidated ready data.
  cache.invalidateMaterials();
  cache.setBudgets(GpuBudget, GpuBudget, 0);
  cache.beginPresentation();
  assert(!cache.request(source, TerrainGpuMaterialResolver3D{}).field);
  cache.endPresentation();
}

void budgetChangesRetireAndRetryWithoutStableFailureLoops() {
  TestResources resources;
  JobSystem jobs(1);
  TerrainGpuFieldCache3D cache(resources, jobs);
  const auto source = fieldForTest();
  constexpr std::size_t Staging = 1024 * 1024;
  cache.setBudgets(247, Staging, 1);
  cache.beginPresentation();
  assert(cache.request(source, TerrainGpuMaterialResolver3D{}).preparing);
  cache.endPresentation();
  finish(jobs, cache);
  cache.beginPresentation();
  assert(!cache.request(source, TerrainGpuMaterialResolver3D{})
              .fallbackReason.empty());
  cache.endPresentation();
  assert(resources.createCalls == 0 && !cache.hasPendingWork());
  cache.setBudgets(247, Staging, 1);
  assert(!cache.request(source, TerrainGpuMaterialResolver3D{}).preparing);
  cache.setBudgets(1024, Staging, 1);
  auto ready = prepareReady(jobs, cache, source);
  cache.setBudgets(1, Staging, 1);
  ready.reset();
  assert(resources.textures.empty());

  cache.setBudgets(1024, 1, 1);
  assert(!cache.request(source, TerrainGpuMaterialResolver3D{}).preparing);
  assert(cache.pendingJobs() == 0);
  cache.setBudgets(1024, Staging, 1);
  assert(prepareReady(jobs, cache, source));
}

void canceledJobsAndMaterialInvalidationRejectStaleResults() {
  TestResources resources;
  JobSystem jobs(1);
  TerrainGpuFieldCache3D cache(resources, jobs);
  std::latch started(1);
  std::latch release(1);
  const auto blocker = jobs.submit([&] {
    started.count_down();
    release.wait();
  });
  started.wait();
  const auto source = fieldForTest();
  cache.beginPresentation();
  assert(cache.request(source, TerrainGpuMaterialResolver3D{}).preparing);
  cache.endPresentation();
  cache.invalidateMaterials();
  release.count_down();
  blocker.wait();
  finish(jobs, cache);
  assert(resources.createCalls == 0 && cache.pendingJobs() == 0);
  assert(prepareReady(jobs, cache, source));
  cache.beginPresentation();
  cache.endPresentation();
  assert(cache.residentBytes() == 0 && resources.textures.empty());
}

void materialInvalidationReusesChannelsButNeverReturnsOldAppearance() {
  TestResources resources;
  JobSystem jobs(1);
  TerrainGpuFieldCache3D cache(resources, jobs);
  auto source = std::make_shared<HeightField>(*fieldForTest());
  source->biomeMaterials = {"asset://material"};
  MaterialBinding binding;
  binding.state = {.blend = BlendMode::Opaque,
                   .depthTest = DepthTest::Less,
                   .cull = CullMode::None,
                   .writeDepth = true};
  const auto resolve = [&](std::string_view) { return &binding; };
  const auto before = prepareReady(jobs, cache, source, resolve);
  binding.baseColor[0] = 0.5F;
  cache.invalidateMaterials();
  cache.beginPresentation();
  const auto pending = cache.request(source, resolve);
  assert(pending.preparing && !pending.field);
  cache.endPresentation();
  finish(jobs, cache);
  cache.beginPresentation();
  const auto after = cache.request(source, resolve).field;
  assert(after && after->height == before->height &&
         after->normal == before->normal);
  assert(after->classification == before->classification);
  assert(after->palette != before->palette && resources.createCalls == 5);
  cache.endPresentation();
}

void materialSnapshotDoesNotBorrowMutableLibraryStorage() {
  TestResources resources;
  JobSystem jobs(1);
  TerrainGpuFieldCache3D cache(resources, jobs);
  auto source = std::make_shared<HeightField>(*fieldForTest());
  source->biomeMaterials = {"asset://material"};
  MaterialBinding binding;
  binding.state = {.blend = BlendMode::Opaque,
                   .depthTest = DepthTest::Less,
                   .cull = CullMode::None,
                   .writeDepth = true};
  binding.baseColor = {0.5F, 1, 1, 1};
  binding.parameters.push_back({.uniform = {.index = 1},
                                .value = binding.baseColor,
                                .name = "base_color"});
  binding.uniforms.push_back({.handle = binding.parameters.front().uniform,
                              .values = binding.parameters.front().value});
  std::latch started(1);
  std::latch release(1);
  const auto blocker = jobs.submit([&] {
    started.count_down();
    release.wait();
  });
  started.wait();
  cache.beginPresentation();
  assert(cache.request(source, [&](std::string_view) { return &binding; })
             .preparing);
  cache.endPresentation();
  binding.baseColor = {0.9F, 1, 1, 1};
  binding.parameters.clear();
  binding.uniforms.clear();
  release.count_down();
  blocker.wait();
  finish(jobs, cache);
  cache.beginPresentation();
  const auto ready =
      cache.request(source, TerrainGpuMaterialResolver3D{}).field;
  assert(ready);
  cache.endPresentation();
  float red = 0;
  std::memcpy(&red, resources.textures.at(ready->palette.index).bytes.data(),
              sizeof(red));
  assert(red == 0.1F);
}

} // namespace

int main() {
  singleFlightReadinessAndBorrowedLifetime();
  appearanceChangesReuseExactChannels();
  paletteDedupChangesRebuildClassification();
  equalSampleCountsCannotReuseDifferentTextureDimensions();
  snapshotFailureLeavesNoDanglingPendingEntry();
  allocationFailureLeavesOldBundleAndCleansNewHandles();
  capabilitiesZeroWorkersAndDisabledBudgetsFallBack();
  zeroPreparationBudgetsRetainValidReadyFields();
  budgetChangesRetireAndRetryWithoutStableFailureLoops();
  canceledJobsAndMaterialInvalidationRejectStaleResults();
  materialInvalidationReusesChannelsButNeverReturnsOldAppearance();
  materialSnapshotDoesNotBorrowMutableLibraryStorage();
  std::cout << "Terrain GPU field cache checks passed\n";
}
