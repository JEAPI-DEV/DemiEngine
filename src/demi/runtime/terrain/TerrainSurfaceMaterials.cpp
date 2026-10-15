#include "demi/runtime/terrain/TerrainSurfaceMaterials.h"

#include "demi/assets/MaterialSet.h"
#include "demi/runtime/terrain/TerrainDrainage.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainHeightField.h"
#include "demi/runtime/terrain/TerrainMaskBuilder.h"
#include "demi/runtime/terrain/TerrainMaterialLayers.h"
#include "demi/runtime/terrain/TerrainPalette.h"

#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace demi::runtime {
namespace {

std::shared_ptr<const TerrainSurfaceClassification>
classifySurface(const HeightField &field, const TerrainRecipe &recipe,
                const TerrainPalette &palette, std::stop_token stop) {
  auto settings = TerrainMaterialLayerSettings::withWaterLevel({}, recipe, {});
  for (const auto &id : field.biomeIds) {
    const auto biome = recipe.biomes.find(id);
    if (biome == recipe.biomes.end()) {
      throw std::invalid_argument(
          "Terrain surface biome is absent from its recipe: " + id);
    }
    settings.biomeAllowsSnow.push_back(biome->second.allowSnow);
  }
  TerrainDrainageSettings drainageSettings;
  drainageSettings.seaLevel = settings.waterLevel;
  const auto drainage =
      computeTerrainDrainage(field, recipe, drainageSettings, stop);
  if (!drainage || stop.stop_requested()) {
    return {};
  }

  TerrainMasks masks;
  if (!buildTerrainMasks(field, recipe, &*drainage,
                         field.surfaceSedimentBalance.get(), masks, true,
                         stop)) {
    return {};
  }

  if (field.graphArtifacts && field.graphArtifacts->waterResult) {
    const auto &coverage = field.graphArtifacts->waterResult->resolvedCoverage;
    if (!coverage) {
      throw std::invalid_argument(
          "Terrain water result has no resolved coverage");
    }
    if (!applyTerrainWaterMasks(field, *coverage, masks, stop)) {
      return {};
    }
  }

  assets::TerrainMaterialSet materialSet;
  materialSet.roles = palette.surfaceMaterials;
  const auto decisions =
      decideTerrainMaterials(materialSet, field, masks, settings);

  // Keep the role table deterministic, independent of sample traversal order.
  std::set<std::string> roleNames;
  for (const auto &decision : decisions) {
    roleNames.insert(decision.roleName);
  }
  constexpr std::size_t roleCapacity =
      std::size_t{std::numeric_limits<std::uint8_t>::max()} + 1;
  if (roleNames.size() > roleCapacity) {
    throw std::invalid_argument(
        "Terrain surface role table exceeds index capacity");
  }

  auto classification = std::make_shared<TerrainSurfaceClassification>();
  classification->roles.assign(roleNames.begin(), roleNames.end());
  classification->samples.resize(decisions.size());
  for (std::size_t sample = 0; sample < decisions.size(); ++sample) {
    if ((sample & 255) == 0 && stop.stop_requested()) {
      return {};
    }
    const auto &roleName = decisions[sample].roleName;
    const auto role = std::ranges::find(classification->roles, roleName);
    const auto roleIndex = std::distance(classification->roles.begin(), role);
    classification->samples.set(sample, static_cast<std::uint8_t>(roleIndex));
  }
  return classification;
}

} // namespace

bool terrainSurfaceMaterialsEqual(const HeightField &left,
                                  const HeightField &right) {
  if (left.surfaceMaterials == right.surfaceMaterials) {
    return true;
  }
  if (!left.surfaceMaterials || !right.surfaceMaterials) {
    return false;
  }
  const auto &leftSurface = *left.surfaceMaterials;
  const auto &rightSurface = *right.surfaceMaterials;
  if (leftSurface.materialSet != rightSurface.materialSet ||
      leftSurface.bindings != rightSurface.bindings) {
    return false;
  }
  if (leftSurface.classification == rightSurface.classification) {
    return true;
  }
  if (!leftSurface.classification || !rightSurface.classification) {
    return false;
  }
  return leftSurface.classification->roles ==
             rightSurface.classification->roles &&
         leftSurface.classification->samples ==
             rightSurface.classification->samples;
}

bool terrainSurfaceBindingsMatch(const HeightField &field,
                                 const TerrainPalette *palette) {
  if (!palette || palette->materialSet.empty()) {
    return !field.surfaceMaterials;
  }
  if (!field.surfaceMaterials) {
    return false;
  }

  const auto &surface = *field.surfaceMaterials;
  return surface.materialSet == palette->materialSet &&
         surface.bindings == palette->surfaceMaterials;
}

bool updateTerrainSurfaceMaterials(HeightField &field,
                                   const TerrainRecipe &recipe,
                                   const TerrainPalette *palette,
                                   bool reclassify, std::stop_token stop) {
  if (stop.stop_requested()) {
    return false;
  }
  if (!palette || palette->materialSet.empty()) {
    field.surfaceMaterials.reset();
    return true;
  }

  auto surface = std::make_shared<TerrainSurfaceMaterials>();
  surface->materialSet = palette->materialSet;
  surface->bindings = palette->surfaceMaterials;
  if (!reclassify && field.surfaceMaterials) {
    surface->classification = field.surfaceMaterials->classification;
  }
  if (!surface->classification) {
    surface->classification = classifySurface(field, recipe, *palette, stop);
    if (!surface->classification) {
      return false;
    }
  }

  for (const auto &role : surface->classification->roles) {
    if (!surface->bindings.contains(role)) {
      surface->unboundRoles.push_back(role);
    }
  }

  // Publish only the complete result. Cancellation leaves the prior state
  // intact.
  if (stop.stop_requested()) {
    return false;
  }
  field.surfaceMaterials = std::move(surface);
  return true;
}
} // namespace demi::runtime
