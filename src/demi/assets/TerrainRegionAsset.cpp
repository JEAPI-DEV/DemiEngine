#include "demi/assets/TerrainRegionAsset.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/DataAsset.h"
#include "demi/assets/DataDocument.h"
#include "demi/assets/DataValueRead.h"
#include "demi/assets/TerrainAsset.h"
#include "demi/runtime/terrain/TerrainPalette.h"

#include <cmath>
#include <stdexcept>

namespace demi::assets {
namespace {
constexpr std::string_view Kind = "terrain region";

const DataValue &required(const DataValue &value, std::string_view field) {
  const auto *result = value.find(field);
  data_read::require(result != nullptr,
                     "Terrain region requires " + std::string(field));
  return *result;
}

double number(const DataValue &value, std::string_view where) {
  data_read::require(value.isNumber(), "Terrain region " + std::string(where) +
                                           " must be numeric");
  const double result =
      value.isInteger()
          ? static_cast<double>(std::get<std::int64_t>(value.value))
          : std::get<double>(value.value);
  data_read::require(std::isfinite(result), "Terrain region " +
                                                std::string(where) +
                                                " must be finite");
  return result;
}
} // namespace

TerrainRegionDefinition parseTerrainRegionAsset(const DataDocument &document) {
  const auto &root = document.root();
  data_read::fields(
      root, Kind, "definition",
      {"format_version", "name", "terrain", "origin", "sampling", "streaming"});
  data_read::require(data_read::integer(required(root, "format_version"), Kind,
                                        "format_version") == 1,
                     "Terrain region format_version must be 1");
  TerrainRegionDefinition result;
  result.terrain = data_read::text(root, "terrain", Kind, "definition", true);
  data_read::require(result.terrain.starts_with("asset://") &&
                         result.terrain.size() > 8,
                     "Terrain region terrain must be an asset:// reference");
  if (const auto *name = root.find("name")) {
    (void)data_read::text(*name, Kind, "name");
  }
  const auto &origin = required(root, "origin");
  data_read::require(origin.isArray() && origin.array()->size() == 2,
                     "Terrain region origin requires X and Z");
  result.originX = number(origin.array()->at(0), "origin X");
  result.originZ = number(origin.array()->at(1), "origin Z");
  const auto &sampling = required(root, "sampling");
  data_read::fields(sampling, Kind, "sampling",
                    {"base_spacing", "normal_spacing"});
  result.baseSampleSpacing =
      number(required(sampling, "base_spacing"), "base_spacing");
  result.normalSampleSpacing =
      number(required(sampling, "normal_spacing"), "normal_spacing");
  data_read::require(result.baseSampleSpacing > 0 &&
                         result.normalSampleSpacing > 0,
                     "Terrain region sample spacing must be positive");
  if (const auto *streaming = root.find("streaming")) {
    data_read::fields(*streaming, Kind, "streaming",
                      {"lod_distance_ratio", "resident_mib", "pending_jobs",
                       "vegetation_cell_samples", "vegetation_radius",
                       "vegetation_canopy_radius", "vegetation_canopy_cell_samples"});
    if (const auto *samples = streaming->find("vegetation_cell_samples")) {
      const auto count = number(*samples, "vegetation_cell_samples");
      data_read::require(
          count >= 1 && count <= 1024 && std::floor(count) == count,
          "Vegetation cell samples must be an integer from 1 to 1024");
      result.streaming.vegetationCellSamples =
          static_cast<std::uint32_t>(count);
    }
    if (const auto *radius = streaming->find("vegetation_radius")) {
      result.streaming.vegetationRadius = number(*radius, "vegetation_radius");
      data_read::require(
          result.streaming.vegetationRadius >= 0 &&
              result.streaming.vegetationRadius <= 10000,
          "Vegetation radius must be between zero and 10000 metres");
    }
    if (const auto *radius = streaming->find("vegetation_canopy_radius")) {
      result.streaming.vegetationCanopyRadius = number(*radius, "vegetation_canopy_radius");
      data_read::require(result.streaming.vegetationCanopyRadius >= 0 &&
                             result.streaming.vegetationCanopyRadius <= 10000,
                         "Vegetation canopy radius must be between zero and 10000 metres");
    }
    if (const auto *samples = streaming->find("vegetation_canopy_cell_samples")) {
      const auto count = number(*samples, "vegetation_canopy_cell_samples");
      data_read::require(count >= 1 && count <= 1024 && std::floor(count) == count,
                         "Vegetation canopy cell samples must be an integer from 1 to 1024");
      result.streaming.vegetationCanopyCellSamples = static_cast<std::uint32_t>(count);
    }
    if (const auto *ratio = streaming->find("lod_distance_ratio")) {
      result.streaming.lodDistanceRatio = number(*ratio, "lod_distance_ratio");
    }
    if (const auto *budget = streaming->find("resident_mib")) {
      const auto value = number(*budget, "resident_mib");
      data_read::require(
          value >= 1 && value <= 65536 && std::floor(value) == value,
          "Terrain region resident_mib must be an integer in [1,65536]");
      result.streaming.maximumResidentBytes =
          std::size_t(value) * 1024U * 1024U;
    }
    if (const auto *jobs = streaming->find("pending_jobs")) {
      const auto value = number(*jobs, "pending_jobs");
      data_read::require(
          value >= 1 && value <= 64 && std::floor(value) == value,
          "Terrain region pending_jobs must be an integer in [1,64]");
      result.streaming.maximumPendingJobs = std::size_t(value);
    }
    data_read::require(result.streaming.lodDistanceRatio >= 1 &&
                           result.streaming.lodDistanceRatio <= 16,
                       "Terrain region lod_distance_ratio must be in [1,16]");
  }
  return result;
}

bool isTerrainRegionAsset(const AssetManifest &manifest) {
  if (manifest.type != "DataAsset") {
    return false;
  }
  const auto metadata = dataAssetMetadata(manifest);
  return manifest.type == "DataAsset" && metadata &&
         metadata->contentType == "terrain_region";
}

std::set<std::string> terrainRegionSourceIds(const AssetRegistry &registry) {
  std::set<std::string> result;
  for (const auto &manifest : registry.assets) {
    if (!isTerrainRegionAsset(manifest)) {
      continue;
    }
    const auto region = loadDataAsset(manifest);
    if (!region) {
      throw std::invalid_argument("Cannot load terrain region " + manifest.id);
    }
    result.insert(parseTerrainRegionAsset(*region->document).terrain);
  }
  return result;
}

runtime::TerrainRegionConfig
loadTerrainRegionSource(const AssetRegistry &registry, std::string_view id) {
  const auto *manifest = findAsset(registry, std::string(id));
  data_read::require(manifest && manifest->type == "DataAsset",
                     "Terrain region requires a registered DataAsset");
  const auto asset = loadDataAsset(*manifest);
  data_read::require(asset && asset->metadata.contentType == "terrain_region",
                     "Expected terrain_region content");
  const auto definition = parseTerrainRegionAsset(*asset->document);
  return compileTerrainRegionSource(registry, definition);
}

runtime::TerrainRegionConfig
compileTerrainRegionSource(const AssetRegistry &registry,
                           const TerrainRegionDefinition &definition) {
  const auto *terrain = findAsset(registry, definition.terrain);
  data_read::require(terrain && terrain->type == "Terrain",
                     "Terrain region must reference an ordinary Terrain asset");
  const auto source = loadTerrainAssetSource(*terrain);
  runtime::TerrainRegionConfig result;
  result.recipe = runtime::TerrainRecipe::parse(source.recipe);
  result.projectDirectory = registry.projectDirectory;
  if (!result.recipe.paletteId.empty()) {
    const auto palette =
        runtime::loadTerrainPalette(registry, result.recipe.paletteId);
    data_read::require(palette.has_value(),
                       "Regional terrain palette could not be loaded");
    result.palette = std::make_shared<const runtime::TerrainPalette>(*palette);
  }
  result.streaming = definition.streaming;
  result.originX = definition.originX;
  result.originZ = definition.originZ;
  result.baseSampleSpacing = definition.baseSampleSpacing;
  result.normalSampleSpacing = definition.normalSampleSpacing;
  data_read::require(
      result.recipe.chunkCells > 1 &&
          (result.recipe.chunkCells & (result.recipe.chunkCells - 1)) == 0,
      "Streamed terrain chunk_cells must be a power of two greater than one");
  (void)runtime::RegionalTerrainSampler(result);
  return result;
}
} // namespace demi::assets
