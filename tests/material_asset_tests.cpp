#include "demi/assets/AssetRegistry.h"
#include "demi/assets/MaterialSurface.h"
#include <nlohmann/json.hpp>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <limits>

using namespace demi;
using namespace demi::assets;

namespace {
using Json = nlohmann::json;

bool rejected(const Json &document) {
  Diagnostics diagnostics;
  const auto material =
      parseMaterialAsset(document, "surface.material.json", &diagnostics);
  assert(material || hasErrors(diagnostics));
  return !material;
}

void defaultsComeFromOrdinaryMaterial() {
  const auto material = parseMaterialAsset({{"format_version", 1}});
  assert(material);
  const auto surface = materialSurface(*material);
  assert(surface.baseColor.r == 1 && surface.baseColor.a == 1);
  assert(surface.roughness == 0.8F && surface.metallic == 0);
  assert(surface.normalStrength == 1 && surface.tiling == 1 &&
         surface.detailStrength == 0);
  assert(surface.heightBlendStrength == 0.5F);
  assert(surface.macroVariationStrength == 0);
  assert(surface.macroVariationScale == 0.02F);
  assert(surface.windStrength == 0 && surface.windFrequency == 1 &&
         surface.windHeight == 1);
  assert(surface.mapping == MaterialMapping::Uv && !surface.normalMapYDown);
  assert(surface.maps.empty());
}

void typedViewUsesTheCanonicalDescriptor() {
  const auto document = Json::parse(R"({
    "format_version":1,
    "mapping":"triplanar",
    "normal_map_convention":"directx",
    "textures":{
      "albedo":"asset://textures/color",
      "normal":"asset://textures/normal",
      "detail_normal":"asset://textures/detail"
    },
    "parameters":{
      "base_color":[0.25,0.5,0.75,1],
      "roughness":0.3,
      "metallic":0.2,
      "normal_strength":2,
      "tiling":4,
      "detail_strength":0.6,
      "height_blend_strength":0.8,
      "macro_variation_strength":0.3,
      "macro_variation_scale":0.01,
      "wind_strength":0.25,
      "wind_frequency":1.5,
      "wind_height":2,
      "foliage_transmission":0.7,
      "ambient_occlusion_strength":0.4
    }
  })");
  const auto material = parseMaterialAsset(document);
  assert(material);
  const auto surface = materialSurface(*material);
  assert(surface.baseColor.r == 0.25F && surface.baseColor.b == 0.75F);
  assert(surface.roughness == 0.3F && surface.metallic == 0.2F);
  assert(surface.normalStrength == 2 && surface.tiling == 4 &&
         surface.detailStrength == 0.6F);
  assert(surface.heightBlendStrength == 0.8F);
  assert(surface.macroVariationStrength == 0.3F);
  assert(surface.macroVariationScale == 0.01F);
  assert(surface.windStrength == 0.25F);
  assert(surface.windFrequency == 1.5F);
  assert(surface.windHeight == 2.F);
  assert(surface.foliageTransmission == 0.7F);
  assert(surface.ambientOcclusionStrength == 0.4F);
  assert(surface.mapping == MaterialMapping::Triplanar &&
         surface.normalMapYDown);
  assert(surface.maps.at(MaterialMapSlot::Albedo) == "asset://textures/color");
  assert(surface.maps.at(MaterialMapSlot::DetailNormal) ==
         "asset://textures/detail");
  assert(!materialMapSlotFromName("base_color") &&
         !materialMapSlotFromName("detail"));
}

void builtinSurfaceDomainsAreValidated() {
  assert(rejected({{"format_version", 1}, {"alpha_cutoff", 0.5}}));
  const auto cutout = parseMaterialAsset(
      {{"format_version", 1},
       {"render_state", {{"alpha_cutoff", 0.5}, {"cull", "none"}}}});
  assert(cutout);
  assert(cutout->renderState.alphaCutoff == 0.5F);
  assert(cutout->renderState.cull == "none");
  assert(rejected(Json::array()));
  assert(rejected(Json::object()));
  assert(rejected({{"format_version", "1"}}));
  assert(rejected({{"format_version", 2}}));
  assert(rejected({{"format_version", 1}, {"mapping", true}}));
  assert(rejected(
      {{"format_version", 1}, {"normal_map_convention", Json::array()}}));
  assert(rejected({{"format_version", 1}, {"textures", Json::array()}}));
  assert(rejected({{"format_version", 1}, {"textures", {{"albedo", 42}}}}));
  for (const auto parameters :
       {Json{{"roughness", -1}},
        Json{{"foliage_transmission", -1}},
        Json{{"foliage_transmission", 1.1}},
        Json{{"ambient_occlusion_strength", 1.1}},
        Json{{"foliage_transmission", {1, 1, 1, 1}}},
        Json{{"ambient_occlusion_strength", {1, 1, 1, 1}}},
        Json{{"metallic", 2}},
        Json{{"normal_strength", -1}},
        Json{{"tiling", 0}},
        Json{{"wind_strength", -1}},
        Json{{"wind_frequency", -1}},
        Json{{"wind_height", 0}},
        Json{{"wind_height", -1}},
        Json{{"wind_strength", {1, 1, 1, 1}}},
        Json{{"wind_frequency", {1, 1, 1, 1}}},
        Json{{"wind_height", {1, 1, 1, 1}}},
        Json{{"tiling", 1e100}},
        Json{{"detail_strength", -1}},
        Json{{"height_blend_strength", -0.1}},
        Json{{"height_blend_strength", 1.1}},
        Json{{"macro_variation_strength", -0.1}},
        Json{{"macro_variation_strength", 1.1}},
        Json{{"macro_variation_scale", 0}},
        Json{{"macro_variation_scale", -1}},
        Json{{"macro_variation_scale", 1e100}},
        Json{{"height_blend_strength", {1, 1, 1, 1}}},
        Json{{"macro_variation_strength", {1, 1, 1, 1}}},
        Json{{"macro_variation_scale", {1, 1, 1, 1}}},
        Json{{"normal_strength", {1, 1, 1, 1}}},
        Json{{"base_color", {1, 2, 1, 1}}},
        Json{{"roughness", std::numeric_limits<double>::infinity()}}}) {
    assert(rejected({{"format_version", 1}, {"parameters", parameters}}));
  }
  assert(rejected({{"format_version", 1}, {"mapping", "world"}}));
  assert(
      rejected({{"format_version", 1}, {"normal_map_convention", "unknown"}}));
  assert(rejected({{"format_version", 1},
                   {"textures", {{"detail", "asset://textures/detail"}}}}));
  assert(rejected({{"format_version", 1}, {"maps", Json::array()}}));
}

void customNamesAndReferenceDependenciesRemainValid() {
  const auto material = parseMaterialAsset({
      {"format_version", 1},
      {"shader", "asset://shaders/custom"},
      {"textures",
       {{"custom_buffer", "asset://targets/history"},
        {"arbitrary_sampler", "asset://textures/image"}}},
      {"parameters", {{"custom_scalar", -12}, {"roughness", 4}}},
  });
  assert(material && material->numbers.at("roughness") == 4);
  assert(rejected({{"format_version", 1},
                   {"shader", "asset://shaders/custom"},
                   {"parameters", {{"custom_overflow", 1e100}}}}));
  assert(rejected({{"format_version", 1},
                   {"shader", "asset://shaders/custom"},
                   {"parameters", {{"custom_color", {1, 1e100, 0, 1}}}}}));
  assert(materialSurface(*material).maps.empty());
  AssetRegistry registry;
  registry.assets = {
      {.id = "asset://shaders/custom", .type = "Shader"},
      {.id = "asset://targets/history", .type = "RenderTarget"},
      {.id = "asset://textures/image", .type = "Texture2D"},
  };
  assert(!hasErrors(validateMaterialAssetReferences(*material, registry, {})));
  assert(materialAssetDependencies(*material) ==
         (std::vector<std::string>{"asset://shaders/custom",
                                   "asset://targets/history",
                                   "asset://textures/image"}));
  registry.assets.back().type = "AudioClip";
  assert(hasErrors(validateMaterialAssetReferences(*material, registry, {})));
}
} // namespace

int main() {
  defaultsComeFromOrdinaryMaterial();
  typedViewUsesTheCanonicalDescriptor();
  builtinSurfaceDomainsAreValidated();
  customNamesAndReferenceDependenciesRemainValid();
  std::cout << "Canonical Material surface checks passed\n";
}
