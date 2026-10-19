#include "demi/runtime/render/bgfx3d/TerrainMaterialArrayCache3D.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <latch>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>

#ifdef NDEBUG
#undef NDEBUG
#endif
// Assertions must remain active in the qualification Release build.
#include <cassert>

using namespace demi::runtime;
using namespace demi::runtime::render;
using Slot = demi::assets::MaterialMapSlot;

namespace {

TerrainMaterialImageSource3D
imageSource(unsigned width, unsigned height,
            const std::vector<unsigned char> &pixels) {
  const auto pixelBytes = std::size_t(width) * height * 4;
  if (pixels.size() != pixelBytes) {
    throw std::invalid_argument("BMP fixture requires complete RGBA pixels");
  }
  auto bytes = std::make_shared<std::vector<std::byte>>(54 + pixelBytes);
  const auto writeInteger = [&](std::size_t offset, std::uint32_t value) {
    for (unsigned index = 0; index < 4; ++index) {
      (*bytes)[offset + index] = std::byte((value >> (8 * index)) & 255);
    }
  };
  (*bytes)[0] = std::byte{'B'};
  (*bytes)[1] = std::byte{'M'};
  writeInteger(2, static_cast<std::uint32_t>(bytes->size()));
  writeInteger(10, 54);
  writeInteger(14, 40);
  writeInteger(18, width);
  writeInteger(22, height);
  (*bytes)[26] = std::byte{1};
  (*bytes)[28] = std::byte{32};
  writeInteger(34, static_cast<std::uint32_t>(pixelBytes));
  // BMP stores BGRA rows from bottom to top; callers provide ordinary RGBA
  // rows from top to bottom, matching the production image decoder output.
  for (unsigned y = 0; y < height; ++y) {
    for (unsigned x = 0; x < width; ++x) {
      const auto input = (std::size_t(y) * width + x) * 4;
      const auto output = 54 + (std::size_t(height - 1 - y) * width + x) * 4;
      (*bytes)[output] = std::byte{pixels[input + 2]};
      (*bytes)[output + 1] = std::byte{pixels[input + 1]};
      (*bytes)[output + 2] = std::byte{pixels[input]};
      (*bytes)[output + 3] = std::byte{pixels[input + 3]};
    }
  }
  return {.encoded = bytes,
          .width = static_cast<std::uint16_t>(width),
          .height = static_cast<std::uint16_t>(height)};
}

TerrainMaterialImageSource3D solidImage(unsigned edge,
                                        std::array<unsigned char, 4> pixel) {
  std::vector<unsigned char> pixels(std::size_t(edge) * edge * 4);
  for (std::size_t offset = 0; offset < pixels.size(); offset += 4) {
    std::copy(pixel.begin(), pixel.end(), pixels.begin() + offset);
  }
  return imageSource(edge, edge, pixels);
}

std::shared_ptr<TerrainMaterialArraySource3D> sourceForTest(unsigned edge = 2) {
  auto source = std::make_shared<TerrainMaterialArraySource3D>();
  source->identity = "material-source";
  source->layers.emplace_back();
  TerrainMaterialLayerSource3D layer{.materialId = "asset://ground"};
  layer.surface.maps[Slot::Albedo] = "asset://color";
  layer.images[Slot::Albedo] = solidImage(edge, {64, 128, 192, 255});
  source->layers.push_back(std::move(layer));
  return source;
}

void packingPreservesChannelsSemanticsAndEveryLayer() {
  auto source = sourceForTest();
  auto &layer = source->layers[1];
  layer.images[Slot::Albedo] =
      imageSource(2, 1, {0, 0, 0, 255, 255, 255, 255, 255});
  layer.images[Slot::AmbientOcclusion] = solidImage(2, {17, 99, 99, 255});
  layer.images[Slot::Roughness] = solidImage(2, {33, 99, 99, 255});
  layer.images[Slot::Metallic] = solidImage(2, {65, 99, 99, 255});
  layer.images[Slot::Height] = solidImage(2, {129, 99, 99, 255});
  layer.images[Slot::Normal] = solidImage(2, {218, 218, 128, 255});
  const auto prototype = layer;
  for (unsigned index = 0; index < 5; ++index) {
    auto extra = prototype;
    extra.materialId = "asset://extra/" + std::to_string(index);
    source->layers.push_back(std::move(extra));
  }
  std::string error;
  const auto packed = prepareTerrainMaterialArrayData3D(*source, 1, error);
  assert(packed && error.empty());
  assert(packed->logicalLayers == 7 && packed->physicalLayers == 7);
  assert(packed->edge == 1 && packed->bytes() == 7 * 5 * 4);
  assert(source->layerIndex("") == 0 &&
         source->layerIndex("asset://extra/4") == 6);
  assert(!source->layerIndex("asset://missing"));
  // The production color resizer must average black and white in linear light.
  for (std::size_t channel = 0; channel < 3; ++channel) {
    assert(packed->pixels[0][4 + channel] == std::byte{188});
  }
  const std::array expectedOrm{std::byte{17}, std::byte{33}, std::byte{65},
                               std::byte{129}};
  for (std::size_t channel = 0; channel < 4; ++channel) {
    assert(packed->pixels[2][4 + channel] == expectedOrm[channel]);
  }
  assert(packed->pixels[1][4] == std::byte{218});
  assert(packed->pixels[3][4] == std::byte{128});
  assert(packed->pixels[3][6] == std::byte{255});
  assert(packed->pixels[4][4] == std::byte{0});
  assert(packed->pixels[0][0] == std::byte{255});
  assert(packed->pixels[2][1] == std::byte{204});
  assert(packed->pixels[2][2] == std::byte{0});
  assert(terrainMaterialArrayReservation3D(*source, 1) >= packed->bytes());

  const auto mips = prepareTerrainMaterialArrayData3D(*source, 2, error);
  assert(mips && mips->pixels[0].size() == 7 * 20);
  // Each complete layer precedes the next layer, including its 1x1 mip.
  assert(mips->pixels[0][20 + 16] == std::byte{188});
  source->layers[1].images[Slot::Albedo].width = 3;
  assert(!prepareTerrainMaterialArrayData3D(*source, 2, error));
  assert(error.find("dimensions changed") != std::string::npos);
  std::stop_source stopped;
  stopped.request_stop();
  assert(!prepareTerrainMaterialArrayData3D(*source, 2, error,
                                            stopped.get_token()));
}

void missingScalarMapsUseAuthoredSurfaceValues() {
  auto source = sourceForTest();
  source->layers[1].surface.roughness = 0.25F;
  source->layers[1].surface.metallic = 0.5F;
  std::string error;
  const auto packed = prepareTerrainMaterialArrayData3D(*source, 1, error);
  assert(packed && packed->pixels[2][5] == std::byte{64});
  assert(packed->pixels[2][6] == std::byte{128});
  source->layers[1].surface.roughness = std::numeric_limits<float>::infinity();
  assert(!prepareTerrainMaterialArrayData3D(*source, 1, error));
}

void absentSlotsUseOneTexelPerLayerWithoutReducingRealMaps() {
  auto source = sourceForTest(4);
  source->layers[1].surface.roughness = 0.25F;
  source->layers[1].surface.metallic = 0.5F;
  std::string error;
  const auto packed = prepareTerrainMaterialArrayData3D(*source, 4, error);
  assert(packed && packed->edge == 4 && packed->logicalLayers == 2);
  const std::array<std::uint16_t, 5> expectedEdges{4, 1, 1, 1, 1};
  assert(packed->arrayEdges == expectedEdges);
  assert(packed->pixels[0].size() == 2 * (64 + 16 + 4));
  for (std::size_t slot = 1; slot < packed->pixels.size(); ++slot) {
    assert(packed->pixels[slot].size() == 2 * 4);
  }
  assert(packed->pixels[2][5] == std::byte{64});
  assert(packed->pixels[2][6] == std::byte{128});
  assert(packed->bytes() == 200);
  assert(packed->bytes() == terrainMaterialArrayBytes3D(*source, 4));
  assert(packed->bytes() <= terrainMaterialArrayReservation3D(*source, 4));
  // A real detail map expands only the detail slot to the common tier.
  source->layers[1].images[Slot::DetailNormal] =
      solidImage(2, {128, 128, 255, 255});
  const auto detail = prepareTerrainMaterialArrayData3D(*source, 4, error);
  assert(detail && detail->arrayEdges[3] == 4 && detail->arrayEdges[4] == 1);
  assert(detail->pixels[3].size() == 2 * (64 + 16 + 4));
  assert(detail->pixels[4].size() == 8);
  assert(detail->bytes() == terrainMaterialArrayBytes3D(*source, 4));
}

void reservationTracksPeakDecodeAndSharedEncodedInputs() {
  auto source = sourceForTest(2);
  const auto initialBytes = terrainMaterialArrayBytes3D(*source, 2);
  const auto initialReservation = terrainMaterialArrayReservation3D(*source, 2);
  for (unsigned index = 0; index < 6; ++index) {
    auto layer = source->layers[1];
    layer.materialId = "asset://shared/" + std::to_string(index);
    source->layers.push_back(std::move(layer));
  }
  const auto output = terrainMaterialArrayBytes3D(*source, 2);
  const auto reservation = terrainMaterialArrayReservation3D(*source, 2);
  // More layers sharing the same inputs add output only, rather than summing
  // sequential decoder work or retaining duplicate encoded snapshots.
  assert(reservation - initialReservation == output - initialBytes);
  for (auto &layer : source->layers) {
    for (auto &[slot, image] : layer.images) {
      image.encodedBytes = image.encoded->size();
      image.encoded.reset();
    }
  }
  assert(terrainMaterialArrayReservation3D(*source, 2) == reservation);
}

void sourceSnapshotsUseCanonicalMaterialsAndContentIdentity() {
  auto directoryTemplate =
      std::filesystem::temp_directory_path() / "demi-material-array-XXXXXX";
  auto buffer = directoryTemplate.string();
  const auto *created = mkdtemp(buffer.data());
  assert(created != nullptr);
  const std::filesystem::path directory(created);
  struct Cleanup {
    std::filesystem::path directory;
    ~Cleanup() { std::filesystem::remove_all(directory); }
  } cleanup{directory};
  const auto image = solidImage(2, {24, 48, 72, 255});
  const auto texturePath = directory / "albedo.png";
  {
    std::ofstream stream(texturePath, std::ios::binary);
    stream.write(reinterpret_cast<const char *>(image.encoded->data()),
                 static_cast<std::streamsize>(image.encoded->size()));
  }
  const auto materialPath = directory / "ground.material.json";
  const nlohmann::json material = {
      {"format_version", 1},
      {"shader", "builtin://lit"},
      {"mapping", "triplanar"},
      {"normal_map_convention", "directx"},
      {"textures", {{"albedo", "asset://color"}}},
      {"parameters", {{"tiling", 0.25}, {"normal_strength", 0.75}}}};
  {
    std::ofstream stream(materialPath);
    stream << material.dump(2);
  }
  demi::AssetRegistry registry;
  registry.assets.push_back(
      {.id = "asset://ground", .type = "Material", .sourcePath = materialPath});
  demi::AssetManifest texture{.id = "asset://color", .type = "Texture2D"};
  texture.sourcePath = texturePath;
  texture.textureSettings.filter = "bilinear";
  texture.textureSettings.wrap = "repeat";
  texture.textureSettings.mipmaps = true;
  registry.assets.push_back(texture);
  std::ranges::sort(registry.assets, {}, &demi::AssetManifest::id);
  std::string error;
  const auto source = createTerrainMaterialArraySource3D(
      registry, {"asset://ground", "", "asset://ground"}, error);
  if (!source || !error.empty()) {
    throw std::runtime_error("Canonical array source fixture failed: " + error);
  }
  assert(source->layers.size() == 2);
  assert(source->layers[1].surface.mapping ==
         demi::assets::MaterialMapping::Triplanar);
  assert(source->layers[1].surface.normalMapYDown);
  assert(source->layers[1].surface.tiling == 0.25F);
  assert(source->layers[1].images.at(Slot::Albedo).width == 2);
  const auto same =
      createTerrainMaterialArraySource3D(registry, {"asset://ground"}, error);
  assert(same && same->identity == source->identity);
  const auto changedImage = solidImage(2, {48, 72, 96, 255});
  {
    std::ofstream stream(texturePath, std::ios::binary);
    stream.write(reinterpret_cast<const char *>(changedImage.encoded->data()),
                 static_cast<std::streamsize>(changedImage.encoded->size()));
  }
  const auto changed =
      createTerrainMaterialArraySource3D(registry, {"asset://ground"}, error);
  assert(changed && changed->identity != source->identity);
  // A worker using the old snapshot cannot read the edited source accidentally.
  const auto oldPacked = prepareTerrainMaterialArrayData3D(*source, 2, error);
  assert(oldPacked && oldPacked->pixels[0][20] == std::byte{24});
  auto textureEntry = std::ranges::find(registry.assets, "asset://color",
                                        &demi::AssetManifest::id);
  assert(textureEntry != registry.assets.end());
  textureEntry->textureSettings.filter = "nearest";
  assert(
      !createTerrainMaterialArraySource3D(registry, {"asset://ground"}, error));
  assert(error.find("nearest") != std::string::npos);
  textureEntry->textureSettings.filter = "bilinear";
  textureEntry->textureSettings.mipmaps = false;
  assert(
      !createTerrainMaterialArraySource3D(registry, {"asset://ground"}, error));
  assert(error.find("mipmapped") != std::string::npos &&
         error.find("asset://color") != std::string::npos);
}

class ArrayResources final : public GpuResources {
public:
  struct Texture {
    std::uint16_t edge;
    std::uint16_t layers;
    bool srgb;
    std::vector<std::byte> pixels;
  };
  RenderDeviceLimits device{.maxTextureSize = 64,
                            .maxTextureSamplers = 16,
                            .supportsInstancing = true,
                            .maxTextureLayers = 64};
  std::map<std::uint32_t, Texture> textures;
  std::size_t calls = 0;
  std::size_t failAt = 0;
  std::thread::id owningThread = std::this_thread::get_id();

  RenderDeviceLimits limits() const override { return device; }
  TextureCapabilities textureCapabilities(TextureFormat) const override {
    return {.sampled2D = true, .sampledArray = true};
  }
  TextureHandle createTexture(const TextureCreateInfo &info,
                              std::string &error) override {
    assert(std::this_thread::get_id() == owningThread);
    ++calls;
    if (calls == failAt) {
      error = "Injected array allocation failure";
      return {};
    }
    assert(info.generateMipmaps && info.layers >= 2 &&
           info.wrap == TextureWrap::Repeat);
    const auto index = static_cast<std::uint32_t>(calls);
    textures.emplace(index, Texture{info.width,
                                    info.layers,
                                    info.srgb,
                                    {info.data.begin(), info.data.end()}});
    return {.index = index, .generation = 1};
  }
  bool destroy(TextureHandle handle) override {
    assert(std::this_thread::get_id() == owningThread);
    assert(textures.erase(handle.index) == 1);
    return true;
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

void finishPreparation(JobSystem &jobs) {
  // The one-worker FIFO places this barrier after every preparation job.
  jobs.submit([] {}).wait();
}

std::shared_ptr<const TerrainMaterialArrays3D>
makeReady(TerrainMaterialArrayCache3D &cache, JobSystem &jobs,
          const std::shared_ptr<const TerrainMaterialArraySource3D> &source,
          std::uint16_t edge) {
  cache.beginPresentation();
  const auto first = cache.request(source, edge);
  assert(!first.arrays && first.preparing);
  finishPreparation(jobs);
  // Completion cannot become visible between cameras in this presentation.
  assert(!cache.request(source, edge).arrays);
  cache.endPresentation();
  cache.beginPresentation();
  const auto ready = cache.request(source, edge).arrays;
  assert(ready);
  cache.endPresentation();
  return ready;
}

void cachePublicationResidencyAndTierReplacement() {
  ArrayResources resources;
  JobSystem jobs(1);
  TerrainMaterialArrayCache3D cache(resources, jobs);
  auto source = sourceForTest(4);
  auto coarse = makeReady(cache, jobs, source, 1);
  assert(resources.calls == 5 && cache.residentBytes() == 40);
  for (std::size_t index = 0; index < coarse->textures.size(); ++index) {
    assert(resources.textures.at(coarse->textures[index].index).srgb ==
           (index == 0 || index == 4));
  }
  cache.beginPresentation();
  auto warming = cache.request(source, 4);
  assert(warming.arrays == coarse && warming.preparing);
  finishPreparation(jobs);
  assert(cache.request(source, 4).arrays == coarse);
  cache.endPresentation();
  cache.beginPresentation();
  auto fine = cache.request(source, 4).arrays;
  assert(fine && fine != coarse && fine->edge == 4);
  assert(resources.textures.at(fine->textures[0].index).edge == 4);
  for (std::size_t slot = 1; slot < fine->textures.size(); ++slot) {
    const auto &neutral = resources.textures.at(fine->textures[slot].index);
    assert(neutral.edge == 1 && neutral.layers == 2 &&
           neutral.pixels.size() == 8);
  }
  cache.endPresentation();
  warming.arrays.reset();
  coarse.reset();
  assert(cache.residentBytes() == fine->residentBytes);
  cache.beginPresentation();
  cache.endPresentation();
  // Retiring cache ownership cannot invalidate borrowed draw bindings.
  assert(resources.textures.size() == 5 &&
         cache.residentBytes() == fine->residentBytes);
  fine.reset();
  assert(resources.textures.empty() && cache.residentBytes() == 0);
}

void failuresAndBudgetsAreBoundedAndRetryableOnSettingsChange() {
  ArrayResources resources;
  JobSystem jobs(1);
  TerrainMaterialArrayCache3D cache(resources, jobs);
  auto source = sourceForTest();
  resources.failAt = 3;
  cache.beginPresentation();
  assert(cache.request(source, 2).preparing);
  cache.endPresentation();
  finishPreparation(jobs);
  cache.beginPresentation();
  const auto failed = cache.request(source, 2);
  assert(!failed.arrays && !failed.preparing);
  assert(failed.fallbackReason.find("Injected") != std::string::npos);
  assert(resources.textures.empty() && cache.residentBytes() == 0);
  cache.endPresentation();
  const auto callsAfterFailure = resources.calls;
  for (unsigned frame = 0; frame < 3; ++frame) {
    cache.beginPresentation();
    assert(!cache.request(source, 2).preparing);
    cache.endPresentation();
  }
  assert(resources.calls == callsAfterFailure && !cache.hasPendingWork());
  cache.setBudgets(4096, 4096, 1);
  resources.failAt = 0;
  auto ready = makeReady(cache, jobs, source, 2);
  assert(ready->residentBytes == 72);
  ready.reset();
  cache.setBudgets(0, 4096, 1);
  cache.beginPresentation();
  assert(!cache.request(source, 2).arrays);
  cache.endPresentation();
  finishPreparation(jobs);
  cache.beginPresentation();
  assert(!cache.request(source, 2).arrays);
  cache.endPresentation();
  assert(!cache.hasPendingWork());
  cache.setBudgets(4096, 1, 1);
  cache.beginPresentation();
  const auto oversized = cache.request(source, 1);
  assert(!oversized.preparing &&
         oversized.fallbackReason.find("staging") != std::string::npos);
  cache.endPresentation();
  assert(!cache.hasPendingWork());

  resources.device.maxTextureLayers = 1;
  cache.beginPresentation();
  assert(cache.request(source, 1).fallbackReason.find("device") !=
         std::string::npos);
  cache.endPresentation();
}

void preparedReplacementWaitsForDrawLeasesWithoutRepeatingWork() {
  ArrayResources resources;
  JobSystem jobs(1);
  TerrainMaterialArrayCache3D cache(resources, jobs);
  cache.setBudgets(90, 4096, 1);
  auto source = sourceForTest(2);
  auto old = makeReady(cache, jobs, source, 1);
  cache.beginPresentation();
  assert(cache.request(source, 2).arrays == old);
  cache.endPresentation();
  finishPreparation(jobs);
  cache.beginPresentation();
  const auto blocked = cache.request(source, 2);
  assert(!blocked.arrays && !blocked.preparing);
  assert(blocked.fallbackReason.find("resident") != std::string::npos);
  cache.endPresentation();
  assert(cache.pendingJobs() == 0 && cache.pendingBytes() != 0);
  assert(!cache.hasPendingWork() && resources.calls == 5);
  old.reset();
  assert(cache.hasPendingWork());
  cache.beginPresentation();
  auto replacement = cache.request(source, 2).arrays;
  assert(replacement && replacement->edge == 2);
  cache.endPresentation();
  assert(cache.pendingBytes() == 0 && resources.calls == 10);
  assert(cache.residentBytes() == replacement->residentBytes);
  for (const auto &layer : replacement->source->layers) {
    for (const auto &[slot, image] : layer.images) {
      assert(!image.encoded);
      assert(image.encodedBytes > 0 && image.width > 0 && image.height > 0);
    }
  }
  // A missing tier asks for encoded data once; it cannot schedule an invalid
  // job against the metadata retained by a ready binding.
  cache.beginPresentation();
  const auto metadataOnly = cache.request(replacement->source, 1);
  assert(metadataOnly.arrays == replacement &&
         metadataOnly.needsSourceSnapshot);
  assert(!metadataOnly.preparing && cache.pendingJobs() == 0);
  cache.endPresentation();
}

void activeCameraUnionPinsEveryRequestedTier() {
  ArrayResources resources;
  JobSystem jobs(1);
  TerrainMaterialArrayCache3D cache(resources, jobs);
  auto source = sourceForTest(4);
  auto coarse = makeReady(cache, jobs, source, 1);
  cache.beginPresentation();
  assert(cache.request(source, 1).arrays == coarse);
  assert(cache.request(source, 4).arrays == coarse);
  cache.endPresentation();
  finishPreparation(jobs);
  cache.beginPresentation();
  auto fine = cache.request(source, 4).arrays;
  assert(fine && fine != coarse);
  assert(cache.request(source, 1).arrays == coarse);
  cache.endPresentation();
  assert(resources.textures.size() == 10);
  fine.reset();
  coarse.reset();
  cache.beginPresentation();
  cache.endPresentation();
  assert(resources.textures.empty());
}

void obsoletePreparationCancelsWithoutBlockingPresentation() {
  ArrayResources resources;
  JobSystem jobs(1);
  TerrainMaterialArrayCache3D cache(resources, jobs);
  std::latch workerEntered(1);
  std::latch releaseWorker(1);
  const auto blocker = jobs.submit([&] {
    workerEntered.count_down();
    releaseWorker.wait();
  });
  workerEntered.wait();
  cache.beginPresentation();
  assert(cache.request(sourceForTest(), 2).preparing);
  cache.endPresentation();
  cache.beginPresentation();
  cache.endPresentation();
  assert(cache.pendingJobs() == 1 && resources.calls == 0);
  // Invalidation is nonblocking even while the shared pool is occupied.
  cache.invalidate();
  releaseWorker.count_down();
  blocker.wait();
  finishPreparation(jobs);
  cache.beginPresentation();
  cache.endPresentation();
  assert(!cache.hasPendingWork() && cache.pendingBytes() == 0);
  assert(resources.calls == 0);
}

void zeroWorkerPoolNeverRunsPreparationInline() {
  ArrayResources resources;
  JobSystem jobs(0);
  TerrainMaterialArrayCache3D cache(resources, jobs);
  cache.beginPresentation();
  const auto result = cache.request(sourceForTest(), 2);
  assert(!result.preparing && !result.arrays && !cache.hasPendingWork());
  assert(resources.calls == 0);
  cache.endPresentation();
}

} // namespace

int main() {
  packingPreservesChannelsSemanticsAndEveryLayer();
  missingScalarMapsUseAuthoredSurfaceValues();
  absentSlotsUseOneTexelPerLayerWithoutReducingRealMaps();
  reservationTracksPeakDecodeAndSharedEncodedInputs();
  sourceSnapshotsUseCanonicalMaterialsAndContentIdentity();
  cachePublicationResidencyAndTierReplacement();
  failuresAndBudgetsAreBoundedAndRetryableOnSettingsChange();
  preparedReplacementWaitsForDrawLeasesWithoutRepeatingWork();
  activeCameraUnionPinsEveryRequestedTier();
  obsoletePreparationCancelsWithoutBlockingPresentation();
  zeroWorkerPoolNeverRunsPreparationInline();
}
