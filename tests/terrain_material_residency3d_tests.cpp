#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/bgfx3d/TerrainMaterialResidency3D.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <chrono>
#include <thread>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

std::shared_ptr<const std::vector<std::byte>> bitmap(unsigned edge) {
  const auto pixelBytes = edge * edge * 4;
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
  writeInteger(18, edge);
  writeInteger(22, edge);
  (*bytes)[26] = std::byte{1};
  (*bytes)[28] = std::byte{32};
  writeInteger(34, pixelBytes);
  std::fill(bytes->begin() + 54, bytes->end(), std::byte{255});
  return bytes;
}

std::shared_ptr<TerrainMaterialArraySource3D> source(unsigned edge) {
  auto result = std::make_shared<TerrainMaterialArraySource3D>();
  result->identity = "material-source";
  result->layers.resize(2);
  auto &material = result->layers[1];
  material.materialId = "asset://material";
  material.surface.maps[demi::assets::MaterialMapSlot::Albedo] =
      "asset://color";
  material.images[demi::assets::MaterialMapSlot::Albedo] = {
      .encoded = bitmap(edge),
      .width = static_cast<std::uint16_t>(edge),
      .height = static_cast<std::uint16_t>(edge)};
  return result;
}

void qualityUsesDistanceDeviceAndCompleteBundleBudget() {
  const auto materials = source(128);
  constexpr std::size_t ample = 8U * 1024U * 1024U;
  assert(terrainMaterialQualityEdge3D(*materials, 128, 1024, ample, 100, 50) ==
         128);
  assert(terrainMaterialQualityEdge3D(*materials, 128, 1024, ample, 100, 150) ==
         64);
  assert(terrainMaterialQualityEdge3D(*materials, 128, 1024, ample, 100, 450) ==
         32);
  assert(terrainMaterialQualityEdge3D(*materials, 128, 16, ample, 100, 50) ==
         16);
  // Five full-mip RGBA8 arrays with two physical layers need 40 bytes even
  // at 1x1. Budget selection accounts for the complete bundle, not one map.
  assert(terrainMaterialQualityEdge3D(*materials, 128, 1024, 40, 100, 0) == 1);
  assert(terrainMaterialQualityEdge3D(*materials, 128, 1024, 39, 100, 0) == 0);
  assert(terrainMaterialQualityEdge3D(*materials, 128, 1024, 0, 100, 0) == 0);
  assert(terrainMaterialWrapCode3D(TextureWrap::Repeat) == 0);
  assert(terrainMaterialWrapCode3D(TextureWrap::Clamp) == 1);
  assert(terrainMaterialWrapCode3D(TextureWrap::Mirror) == 2);
}

TerrainMaterialArrayRequest3D warm(TerrainMaterialResidency3D &residency,
                                   const TerrainGpuField3D &field,
                                   float distance, unsigned expectedEdge) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(3);
  TerrainMaterialArrayRequest3D result;
  do {
    residency.beginPresentation();
    result = residency.request(field, distance);
    residency.endPresentation();
    if (result.arrays && result.arrays->edge == expectedEdge) {
      return result;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  } while (std::chrono::steady_clock::now() < deadline);
  assert(false && "Terrain material tier did not become ready");
  return result;
}

} // namespace

int main() {
  qualityUsesDistanceDeviceAndCompleteBundleBudget();
  BgfxGraphicsDevice graphics;
  std::string error;
  assert(graphics.initialize(
      {.api = GraphicsApi::Noop, .width = 8, .height = 8, .vsync = false},
      error));
  auto resources = createBgfxGpuResources();
  {
    JobSystem jobs(1);
    unsigned snapshots = 0;
    std::weak_ptr<const std::vector<std::byte>> encodedInputs;
    TerrainMaterialResidency3D residency(
        *resources, jobs,
        [&](const demi::AssetRegistry &, const std::vector<std::string> &,
            std::string &) {
          ++snapshots;
          auto materials = source(64);
          encodedInputs = materials->layers[1].images.begin()->second.encoded;
          return materials;
        });
    residency.loadAssets({});
    constexpr std::size_t budget = 8U * 1024U * 1024U;
    residency.setQuality(64, budget, 100, budget, 2);
    TerrainGpuField3D field;
    field.mapped = true;
    field.materialIds = {"asset://material", "asset://material"};
    auto near = warm(residency, field, 0, 64);
    assert(snapshots == 1);
    assert(encodedInputs.expired());
    residency.beginPresentation();
    const auto far = residency.request(field, 500);
    const auto anotherNearCamera = residency.request(field, 0);
    assert(far.arrays == near.arrays);
    assert(anotherNearCamera.arrays == near.arrays);
    residency.endPresentation();
    // A far camera cannot lower a near camera's demand in the same frame or
    // immediately in the next frame, regardless of extraction order.
    residency.beginPresentation();
    const auto precedingNearDemand = residency.request(field, 500);
    assert(precedingNearDemand.arrays == near.arrays);
    residency.endPresentation();
    auto distant = warm(residency, field, 500, 16);
    assert(distant.arrays && snapshots == 2);
    assert(residency.residentBytes() >= distant.arrays->residentBytes);
    near.arrays.reset();
    distant.arrays.reset();
    residency.setQuality(64, 0, 100, budget, 2);
    residency.beginPresentation();
    const auto disabled = residency.request(field, 0);
    assert(!disabled.arrays && !disabled.fallbackReason.empty());
    residency.endPresentation();
    assert(snapshots == 2);
    residency.loadAssets({});
    residency.setQuality(64, budget, 100, budget, 2);
    static_cast<void>(warm(residency, field, 0, 64));
    assert(snapshots == 3);
    residency.clear();
  }
  {
    JobSystem jobs(0);
    unsigned failedSnapshots = 0;
    TerrainMaterialResidency3D residency(
        *resources, jobs,
        [&](const demi::AssetRegistry &, const std::vector<std::string> &,
            std::string &error) {
          ++failedSnapshots;
          error = "Authored map is missing";
          return std::shared_ptr<const TerrainMaterialArraySource3D>{};
        });
    TerrainGpuField3D field;
    field.materialIds = {"asset://missing-material"};
    for (unsigned frame = 0; frame < 3; ++frame) {
      residency.beginPresentation();
      const auto missing = residency.request(field, 0);
      assert(!missing.arrays &&
             missing.fallbackReason == "Authored map is missing");
      residency.endPresentation();
    }
    assert(failedSnapshots == 1);
    residency.loadAssets({});
    residency.beginPresentation();
    static_cast<void>(residency.request(field, 0));
    residency.endPresentation();
    assert(failedSnapshots == 2);
  }
  resources.reset();
  graphics.shutdown();
}
