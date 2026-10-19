#include "demi/runtime/render/bgfx3d/TerrainMaterialResidency3D.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <limits>

namespace demi::runtime::render {
namespace {

std::shared_ptr<const TerrainMaterialArraySource3D> metadataSource(
    const std::shared_ptr<const TerrainMaterialArraySource3D> &source) {
  bool hasEncodedInputs = false;
  for (const auto &layer : source->layers) {
    for (const auto &[slot, image] : layer.images) {
      static_cast<void>(slot);
      hasEncodedInputs = hasEncodedInputs || bool(image.encoded);
    }
  }
  if (!hasEncodedInputs) {
    return source;
  }
  auto metadata = std::make_shared<TerrainMaterialArraySource3D>(*source);
  for (auto &layer : metadata->layers) {
    for (auto &[slot, image] : layer.images) {
      static_cast<void>(slot);
      if (image.encoded) {
        image.encodedBytes = image.encoded->size();
        image.encoded.reset();
      }
    }
  }
  return metadata;
}

std::uint32_t sourceEdge(const TerrainMaterialArraySource3D &source) {
  std::uint32_t edge = 1;
  for (const auto &layer : source.layers) {
    for (const auto &[slot, image] : layer.images) {
      static_cast<void>(slot);
      edge = std::max(
          {edge, std::uint32_t(image.width), std::uint32_t(image.height)});
    }
  }
  return edge;
}

} // namespace

std::uint16_t terrainMaterialQualityEdge3D(
    const TerrainMaterialArraySource3D &source, std::uint32_t maximumEdge,
    std::uint32_t deviceMaximum, std::size_t gpuBytes, float detailDistance,
    float nearestVisibleDistance) {
  if (source.layers.empty() || maximumEdge == 0 || deviceMaximum == 0 ||
      gpuBytes == 0 || !std::isfinite(detailDistance) || detailDistance <= 0 ||
      !std::isfinite(nearestVisibleDistance)) {
    return 0;
  }
  const double distance = std::max(0.0, double(nearestVisibleDistance));
  if (distance > double(detailDistance) * 4) {
    maximumEdge = std::max(1U, maximumEdge / 4);
  } else if (distance > detailDistance) {
    maximumEdge = std::max(1U, maximumEdge / 2);
  }
  auto edge = std::min({maximumEdge, deviceMaximum, sourceEdge(source),
                        std::uint32_t(UINT16_MAX)});
  while (edge > 0 && terrainMaterialArrayBytes3D(
                         source, static_cast<std::uint16_t>(edge)) > gpuBytes) {
    edge = edge == 1 ? 0 : std::max(1U, edge / 2);
  }
  return static_cast<std::uint16_t>(edge);
}

TerrainMaterialResidency3D::TerrainMaterialResidency3D(
    GpuResources &resources, JobSystem &jobs, SourceFactory sourceFactory)
    : resources_(resources), arrays_(resources, jobs),
      sourceFactory_(sourceFactory ? std::move(sourceFactory)
                                   : createTerrainMaterialArraySource3D) {}

void TerrainMaterialResidency3D::loadAssets(const AssetRegistry &registry) {
  clear();
  registry_ = registry;
}

void TerrainMaterialResidency3D::setQuality(std::uint32_t maximumEdge,
                                            std::size_t gpuBytes,
                                            float detailDistance,
                                            std::size_t stagingBytes,
                                            std::size_t preparationJobs) {
  const bool changed = maximumEdge_ != maximumEdge || gpuBytes_ != gpuBytes ||
                       detailDistance_ != detailDistance;
  maximumEdge_ = maximumEdge;
  gpuBytes_ = gpuBytes;
  detailDistance_ = detailDistance;
  qualityError_.clear();
  if (maximumEdge == 0 || !std::isfinite(detailDistance) ||
      detailDistance <= 0) {
    qualityError_ = "Terrain material texture quality requires positive "
                    "resolution and finite detail distance.";
  }
  if (changed) {
    // An old quality request must not override an explicit user quality edit.
    for (auto &[key, entry] : sources_) {
      static_cast<void>(key);
      entry.currentDemand = 0;
      entry.previousDemand = 0;
    }
  }
  arrays_.setBudgets(gpuBytes, stagingBytes, preparationJobs);
}

void TerrainMaterialResidency3D::beginPresentation() {
  arrays_.beginPresentation();
  for (auto &[key, entry] : sources_) {
    static_cast<void>(key);
    entry.previousDemand = entry.currentDemand;
    entry.currentDemand = 0;
  }
}

void TerrainMaterialResidency3D::endPresentation() {
  // Keep the preceding presentation's camera union for one frame. This avoids
  // tier churn when camera order or a scheduled target's cadence changes.
  for (const auto &[key, entry] : sources_) {
    static_cast<void>(key);
    if (entry.source && entry.previousDemand > 0 && entry.currentDemand == 0) {
      static_cast<void>(arrays_.request(entry.source, entry.previousDemand));
    }
  }
  arrays_.endPresentation();
}

void TerrainMaterialResidency3D::clear() {
  arrays_.clear();
  sources_.clear();
}

TerrainMaterialArrayRequest3D
TerrainMaterialResidency3D::request(const TerrainGpuField3D &field,
                                    float nearestVisibleChunkDistance) {
  if (!qualityError_.empty()) {
    return {.fallbackReason = qualityError_};
  }
  if (gpuBytes_ == 0) {
    return {.fallbackReason = "GPU terrain material arrays are disabled by "
                              "their texture budget."};
  }
  auto ids = field.materialIds;
  std::ranges::sort(ids);
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  std::erase(ids, std::string{});
  const auto limits = resources_.limits();
  const auto physicalLayers = std::max(std::size_t{2}, ids.size() + 1);
  if (!resources_.textureCapabilities(TextureFormat::RGBA8).sampledArray ||
      physicalLayers > limits.maxTextureLayers || physicalLayers > UINT16_MAX) {
    return {.fallbackReason =
                "Terrain material arrays exceed this device's layer support."};
  }
  auto [iterator, inserted] = sources_.try_emplace(std::move(ids));
  auto &entry = iterator->second;
  const auto snapshot = [&] {
    try {
      auto replacement =
          sourceFactory_(registry_, iterator->first, entry.error);
      if (replacement) {
        entry.source = std::move(replacement);
      } else if (entry.error.empty()) {
        entry.error = "Terrain material source snapshot could not be created.";
      }
    } catch (const std::exception &exception) {
      entry.error = exception.what();
    }
    if (!entry.source && entry.error.empty()) {
      entry.error = "Terrain material source snapshot could not be created.";
    }
  };
  if (inserted) {
    snapshot();
  }
  if (!entry.source) {
    return {.fallbackReason = entry.error};
  }
  const auto edge = terrainMaterialQualityEdge3D(
      *entry.source, maximumEdge_, limits.maxTextureSize, gpuBytes_,
      detailDistance_, nearestVisibleChunkDistance);
  if (edge == 0) {
    return {.fallbackReason = "Terrain material array bundle cannot fit the "
                              "device or texture budget."};
  }
  entry.currentDemand = std::max(entry.currentDemand, edge);
  const auto presentationEdge =
      std::max(entry.currentDemand, entry.previousDemand);
  auto result = arrays_.request(entry.source, presentationEdge);
  if (result.needsSourceSnapshot) {
    if (entry.error.empty()) {
      snapshot();
    }
    if (!entry.error.empty()) {
      result.fallbackReason = entry.error;
      result.needsSourceSnapshot = false;
    } else {
      result = arrays_.request(entry.source, presentationEdge);
    }
  }
  // Only admitted preparation jobs retain encoded inputs. Warm source memo
  // keeps dimensions, material parameters and identity, never image payloads.
  if (result.arrays) {
    entry.source = result.arrays->source;
  } else if (entry.source) {
    entry.source = metadataSource(entry.source);
  }
  return result;
}

bool TerrainMaterialResidency3D::hasPendingWork() const {
  return arrays_.hasPendingWork();
}
std::size_t TerrainMaterialResidency3D::residentBytes() const {
  return arrays_.residentBytes();
}
std::size_t TerrainMaterialResidency3D::pendingBytes() const {
  return arrays_.pendingBytes();
}
std::size_t TerrainMaterialResidency3D::pendingJobs() const {
  return arrays_.pendingJobs();
}
std::size_t TerrainMaterialResidency3D::uploadBytes() const {
  return arrays_.uploadBytes();
}

} // namespace demi::runtime::render
