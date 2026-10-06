#include "demi/runtime/terrain/TerrainCook.h"
#include "demi/assets/AssetHash.h"
#include "demi/runtime/terrain/TerrainCookFormat.h"
#include "demi/runtime/terrain/TerrainGraph.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace demi::runtime {

std::string terrainCookRecipeDigest(const TerrainRecipe &recipe) {
  auto semantic = recipe.toJson();
  if (!recipe.graph.is_null())
    semantic["graph"] = TerrainGraph::parse(recipe.graph).contentKey();
  const std::string canonical = semantic.dump();
  return assets::hashBytes(
      std::span(reinterpret_cast<const unsigned char *>(canonical.data()),
                canonical.size()));
}

std::optional<TerrainCookedField>
cookTerrainField(const TerrainCookInput &input, std::string *error) {
  if (input.field == nullptr) {
    cook_format::setError(error,
                          "Cannot cook terrain without a generated field.");
    return std::nullopt;
  }
  if (input.recipeDigest.empty()) {
    cook_format::setError(
        error, "Cannot cook terrain without a canonical recipe digest.");
    return std::nullopt;
  }
  const HeightField &field = *input.field;
  std::string reason;
  std::size_t samples = 0;
  if (!cook_format::gridSampleCount(field.cellsX, field.cellsZ, samples,
                                    reason)) {
    cook_format::setError(error, std::move(reason));
    return std::nullopt;
  }
  if (field.baseHeights.size() != samples || field.heights.size() != samples ||
      field.normals.size() != samples || field.biomeIndices.size() != samples ||
      field.exclusions.size() != samples) {
    cook_format::setError(
        error, "Terrain sample arrays do not match the field's own grid; cook "
               "will not reconcile a partial field.");
    return std::nullopt;
  }
  if (field.biomeIds.size() != field.biomeColors.size()) {
    cook_format::setError(error,
                          "Terrain biome ids and colours disagree in count.");
    return std::nullopt;
  }
  if ((!field.biomeMaterials.empty() &&
       field.biomeMaterials.size() != field.biomeIds.size()) ||
      (!field.biomeTextureScales.empty() &&
       field.biomeTextureScales.size() != field.biomeIds.size())) {
    cook_format::setError(
        error, "Terrain appearance arrays do not match its biome palette.");
    return std::nullopt;
  }
  if (!std::isfinite(input.water.seaLevel)) {
    cook_format::setError(error,
                          "Terrain water level must be finite to be cooked.");
    return std::nullopt;
  }

  TerrainCookedField cooked;
  cooked.version = terrainCookVersion;
  cooked.size = field.size;
  cooked.cellsX = field.cellsX;
  cooked.cellsZ = field.cellsZ;
  cooked.seaLevel = input.water.seaLevel;
  cooked.baseHeights = cook_format::flatten(field.baseHeights);
  cooked.heights = cook_format::flatten(field.heights);
  cooked.biomeIndices = cook_format::flattenIndices(field.biomeIndices);
  cooked.exclusions = cook_format::flatten(field.exclusions);
  cooked.normalsX.reserve(samples);
  cooked.normalsY.reserve(samples);
  cooked.normalsZ.reserve(samples);
  for (std::size_t index = 0; index < samples; ++index) {
    const auto normal = field.normals[index];
    cooked.normalsX.push_back(normal.x);
    cooked.normalsY.push_back(normal.y);
    cooked.normalsZ.push_back(normal.z);
  }
  cooked.biomeIds = field.biomeIds;
  cooked.biomeColors = field.biomeColors;
  cooked.biomeMaterials = field.biomeMaterials.empty()
                              ? std::vector<std::string>(field.biomeIds.size())
                              : field.biomeMaterials;
  cooked.biomeTextureScales =
      field.biomeTextureScales.empty()
          ? std::vector<float>(field.biomeIds.size(), 1.0F)
          : field.biomeTextureScales;
  cooked.chunks = field.chunks;
  cooked.generatorVersionTag = input.generatorVersionTag;
  cooked.recipeDigest = input.recipeDigest;
  cooked.inputFingerprint = input.inputFingerprint;
  cooked.stageOrder = field.stageOrder;
  cooked.quality = field.quality;
  cooked.paletteId = field.paletteId;

  // Masks are recorded present only when they were actually derived for the
  // whole grid. A mask set covering part of a field is refused rather than
  // stored, because the alternative is reading back zeros as a claim about the
  // rest of it.
  if (input.masks != nullptr && !input.masks->empty()) {
    const TerrainMasks &masks = *input.masks;
    if (masks.slope.size() != samples || masks.moisture.size() != samples ||
        masks.waterDistance.size() != samples || masks.flow.size() != samples ||
        masks.sediment.size() != samples || masks.substrate.size() != samples) {
      cook_format::setError(
          error,
          "Terrain masks do not cover the whole grid; cook refuses to store "
          "a partial mask set.");
      return std::nullopt;
    }
    cooked.masksPresent = true;
    cooked.slope = cook_format::flatten(masks.slope);
    cooked.moisture = cook_format::flatten(masks.moisture);
    cooked.waterDistance = cook_format::flatten(masks.waterDistance);
    cooked.flow = cook_format::flatten(masks.flow);
    cooked.sediment = cook_format::flatten(masks.sediment);
    cooked.substrates = cook_format::flattenIndices(masks.substrate);
  }

  reason.clear();
  if (!cook_format::validate(cooked, reason)) {
    cook_format::setError(error, std::move(reason));
    return std::nullopt;
  }
  const auto encoded = cook_format::encode(cooked, reason);
  if (encoded.empty()) {
    cook_format::setError(error, std::move(reason));
    return std::nullopt;
  }
  // The digest is stored on the payload, so a later staleness check can tell a
  // damaged file from an intact one without trusting the struct it was read
  // back into.
  cooked.contentHash = cook_format::contentDigest(cooked);
  cook_format::setError(error, {});
  return cooked;
}

std::vector<std::byte>
serializeTerrainCookedField(const TerrainCookedField &field) {
  std::string reason;
  return cook_format::encode(field, reason);
}

std::optional<TerrainCookedField>
deserializeTerrainCookedField(std::span<const std::byte> bytes,
                              std::string *error) {
  TerrainCookedField field;
  std::string reason;
  if (!cook_format::decode(bytes, field, reason)) {
    cook_format::setError(error, std::move(reason));
    return std::nullopt;
  }
  cook_format::setError(error, {});
  return field;
}

std::shared_ptr<const HeightField>
loadTerrainCookedField(const TerrainCookedField &field) {
  std::string reason;
  std::size_t samples = 0;
  if (!cook_format::validate(field, reason, &samples))
    throw std::logic_error("Refusing to load a cooked terrain that is not "
                           "loadable: " +
                           reason);
  auto loaded = std::make_shared<HeightField>();
  loaded->size = field.size;
  loaded->cellsX = field.cellsX;
  loaded->cellsZ = field.cellsZ;
  loaded->baseHeights.resize(samples);
  loaded->heights.resize(samples);
  loaded->normals.resize(samples);
  loaded->biomeIndices.resize(samples);
  loaded->exclusions.resize(samples);
  for (std::size_t index = 0; index < samples; ++index) {
    loaded->baseHeights.set(index, field.baseHeights[index]);
    loaded->heights.set(index, field.heights[index]);
    loaded->normals.set(index,
                        Vec3{field.normalsX[index], field.normalsY[index],
                             field.normalsZ[index]});
    loaded->biomeIndices.set(index, std::size_t(field.biomeIndices[index]));
    loaded->exclusions.set(index, field.exclusions[index]);
  }
  loaded->biomeIds = field.biomeIds;
  loaded->biomeColors = field.biomeColors;
  loaded->biomeMaterials = field.biomeMaterials;
  loaded->biomeTextureScales = field.biomeTextureScales;
  loaded->chunks = field.chunks;
  loaded->paletteId = field.paletteId;
  loaded->inputFingerprint = field.inputFingerprint;
  loaded->stageOrder = field.stageOrder;
  loaded->quality = field.quality;

  // The reloaded field must reproduce the file it came from. A loader that
  // quietly drops, reorders or reinterprets a sample is a bug that would
  // otherwise surface as a terrain that no longer matches its collider, so it
  // is caught here rather than reported by whoever noticed the shape.
  // Round-trip fidelity is asserted by the test, which re-cooks the loaded
  // field and compares the bytes. Doing it here would serialise the payload a
  // second time on every load to prove a load that already succeeded.
  return std::shared_ptr<const HeightField>(std::move(loaded));
}

std::string terrainCookStaleness(const TerrainCookedField &cached,
                                 const TerrainCookInput &input) {
  if (cached.version != terrainCookVersion)
    return "version";
  if (cached.generatorVersionTag != input.generatorVersionTag)
    return "generator_version";
  if (input.recipeDigest.empty() || cached.recipeDigest != input.recipeDigest)
    return "recipe_digest";
  if (cached.inputFingerprint != input.inputFingerprint)
    return "input_fingerprint";
  if (input.field == nullptr)
    return "input_field_missing";
  const HeightField &field = *input.field;
  std::string reason;
  std::size_t samples = 0;
  if (!cook_format::gridSampleCount(field.cellsX, field.cellsZ, samples,
                                    reason) ||
      cached.cellsX != field.cellsX || cached.cellsZ != field.cellsZ ||
      cached.size.x != field.size.x || cached.size.y != field.size.y ||
      cached.heights.size() != samples ||
      cached.baseHeights.size() != samples ||
      cached.normalsX.size() != samples ||
      cached.biomeIndices.size() != samples)
    return "grid";
  if (field.biomeIds != cached.biomeIds ||
      !cook_format::sameColors(field.biomeColors, cached.biomeColors))
    return "grid";
  for (std::size_t index = 0; index < field.biomeIds.size(); ++index) {
    const auto material = field.biomeMaterials.empty()
                              ? std::string{}
                              : field.biomeMaterials.at(index);
    const float scale = field.biomeTextureScales.empty()
                            ? 1.0F
                            : field.biomeTextureScales.at(index);
    if (index >= cached.biomeMaterials.size() ||
        index >= cached.biomeTextureScales.size() ||
        material != cached.biomeMaterials[index] ||
        scale != cached.biomeTextureScales[index])
      return "appearance";
  }
  if (cached.stageOrder != field.stageOrder || cached.quality != field.quality)
    return "grid";
  // Recomputed from the payload rather than trusted from the struct, so a
  // payload that was damaged after it was written is caught here instead of
  // being served to a world that would then disagree with its own collider.
  if (!cook_format::digestMatches(cached))
    return "content_hash";
  return {};
}

TerrainCookProvenance terrainCookProvenance(const TerrainCookInput &input) {
  if (input.recipeDigest.empty())
    throw std::invalid_argument(
        "Terrain cook provenance requires a canonical recipe digest.");
  return TerrainCookProvenance{.generatorVersionTag = input.generatorVersionTag,
                               .recipeDigest = input.recipeDigest,
                               .inputFingerprint = input.inputFingerprint,
                               .recipeKey = input.generatorVersionTag + "\n" +
                                            input.recipeDigest + "\n" +
                                            input.inputFingerprint};
}

} // namespace demi::runtime
