#include "demi/runtime/render/MaterialLibrary.h"
#include "demi/runtime/render/backend/ImageColorSpace.h"
#include "demi/runtime/render/bgfx3d/TerrainGpuFieldData3D.h"
#include "demi/runtime/terrain/TerrainHeightField.h"
#include "demi/runtime/terrain/TerrainSurfaceMaterials.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <map>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

using uint = std::uint32_t;
float max(float first, float second) { return std::max(first, second); }
struct vec2 {
  float x;
  float y;
};

#include "demi/runtime/render/bgfx3d/shaders/demi_terrain_blending.sh"

void sharpMaterialsFollowCellTrianglesWithoutInterpolatingIds() {
  assert(terrainHeightBlendFactor(0, 0) == 1);
  assert(terrainHeightBlendFactor(1, 0) == 1);
  assert(terrainHeightBlendFactor(0.5F, 1) == 1);
  assert(terrainHeightBlendFactor(0, 0.5F) == 0.5F);
  assert(terrainHeightBlendFactor(1, 0.5F) == 1.5F);
  assert(terrainHeightBlendFactor(0, 1) > 0);
  // With equal base weights and default strength, height 1 versus 0 gives a
  // 3:1 preference. A material outside the original footprint stays absent.
  const auto lower = 0.5F * terrainHeightBlendFactor(0, 0.5F);
  const auto upper = 0.5F * terrainHeightBlendFactor(1, 0.5F);
  assert(upper / (upper + lower) == 0.75F);
  assert(0 * terrainHeightBlendFactor(1, 1) == 0);

  assert(terrainTriangleMaterialIndex(9, 5, 5, 2, {0.2F, 0.3F}) == 5);
  assert(terrainTriangleMaterialIndex(9, 5, 7, 7, {0.9F, 0.7F}) == 7);
  assert(terrainTriangleMaterialIndex(9, 5, 7, 2, {0.2F, 0.3F}) == 9);
  assert(terrainTriangleMaterialIndex(9, 5, 7, 2, {0.9F, 0.7F}) == 5);
  assert(terrainTriangleMaterialIndex(9, 5, 7, 2, {0.5F, 0.5F}) == 9);
  assert(terrainTriangleMaterialIndex(9, 5, 9, 2, {0.2F, 0.3F}) == 9);
  assert(terrainTriangleMaterialIndex(9, 5, 7, 5, {0.9F, 0.7F}) == 5);
  constexpr uint largeId = 0x01000001U;
  assert(terrainTriangleMaterialIndex(largeId, 2, 3, 4, {0, 0}) == largeId);
}

MaterialBinding material(std::array<float, 4> color) {
  MaterialBinding binding;
  binding.state = {.blend = BlendMode::Opaque,
                   .depthTest = DepthTest::Less,
                   .cull = CullMode::Clockwise,
                   .writeDepth = true};
  binding.baseColor = color;
  return binding;
}

HeightField fixture() {
  HeightField field;
  field.cellsX = 2;
  field.cellsZ = 1;
  field.size = {4, 3};
  field.heights = {2, 3, 5, 7, 11, 13};
  field.normals = {{0.6F, 0.8F, 0}, {0, 1, 0}, {0, 0.8F, 0.6F},
                   {0, 1, 0},       {0, 1, 0}, {0, 1, 0}};
  field.biomeIds = {"grass", "rock"};
  field.biomeColors = {{0.5F, 0.8F, 0.3F, 1}, {1, 0.25F, 0.5F, 1}};
  field.biomeIndices = {0, 0, 1, 1, 0, 1};
  auto classification = std::make_shared<TerrainSurfaceClassification>();
  classification->roles = {"earth", "stone"};
  classification->samples = {0, 1, 0, 1, 0, 1};
  auto surfaces = std::make_shared<TerrainSurfaceMaterials>();
  surfaces->classification = classification;
  surfaces->bindings = {{"earth", "asset://earth"}, {"stone", "asset://stone"}};
  field.surfaceMaterials = surfaces;
  return field;
}

std::uint32_t decode(const std::array<std::uint8_t, 4> bytes) {
  return std::uint32_t(bytes[0]) | (std::uint32_t(bytes[1]) << 8U) |
         (std::uint32_t(bytes[2]) << 16U) | (std::uint32_t(bytes[3]) << 24U);
}

void samplesAndPaletteRetainCanonicalValues() {
  const auto field = fixture();
  std::map<std::string, MaterialBinding, std::less<>> materials;
  materials.emplace("asset://earth", material({0.8F, 0.6F, 0.4F, 1}));
  materials.emplace("asset://stone", material({0.4F, 0.5F, 0.6F, 1}));
  materials.at("asset://earth").metallic = 0.1F;
  materials.at("asset://earth").roughness = 0.7F;
  materials.at("asset://stone").metallic = 0.3F;
  materials.at("asset://stone").roughness = 0.6F;
  materials.at("asset://stone").unlit = true;
  std::map<std::string, int, std::less<>> calls;
  const auto resolve = [&](std::string_view id) -> const MaterialBinding * {
    ++calls[std::string(id)];
    return &materials.at(std::string(id));
  };
  const auto result = prepareTerrainGpuFieldData3D(field, resolve, 16);
  assert(result.compatible && result.fallbackReason.empty());
  assert(result.sampleWidth == 3 && result.sampleHeight == 2);
  assert(result.state.cull == CullMode::Clockwise);
  assert(result.paletteEntries == 4);
  assert(calls.at("asset://earth") == 1 && calls.at("asset://stone") == 1);
  for (std::size_t sample = 0; sample < field.heights.size(); ++sample) {
    assert(result.heights[sample] == field.heights.at(sample));
    const auto normal = field.normals.at(sample);
    const std::array<float, 4> expectedNormal{normal.x, normal.y, normal.z, 0};
    assert(result.normals[sample] == expectedNormal);
    const auto index = decode(result.materialIndices[sample]);
    assert(index < result.paletteEntries);
    const auto &binding = materials.at(sample == 0 || sample == 2 || sample == 4
                                           ? "asset://earth"
                                           : "asset://stone");
    const auto tint = field.biomeColors.at(field.biomeIndices.at(sample));
    const std::array<float, 4> expectedColor{
        static_cast<float>(decodeSrgbChannel(tint.r * binding.baseColor[0])),
        static_cast<float>(decodeSrgbChannel(tint.g * binding.baseColor[1])),
        static_cast<float>(decodeSrgbChannel(tint.b * binding.baseColor[2])),
        1};
    const std::array<float, 4> expectedParameters{
        binding.metallic, binding.roughness, binding.unlit ? 1.0F : 0.0F,
        binding.state.cull == CullMode::Clockwise ? 1.0F : 0.0F};
    assert(result.palette[index * 2] == expectedColor);
    assert(result.palette[index * 2 + 1] == expectedParameters);
  }
  assert(result.materialIndices[0] == result.materialIndices[4]);
  assert(result.materialIndices[3] == result.materialIndices[5]);
}

void paletteDecodesCombinedSrgbTintForSolidAndMappedMaterials() {
  auto field = fixture();
  field.biomeMaterials = {"asset://override", "asset://override"};
  field.biomeColors = {{0.5F, 0.02F, 1.F, 1.F}, {0.5F, 0.02F, 1.F, 1.F}};
  auto binding = material({0.5F, 0.5F, 0.5F, 1.F});
  const auto resolve = [&](std::string_view) { return &binding; };
  for (const bool mapped : {false, true}) {
    if (mapped) {
      binding.surface.maps[demi::assets::MaterialMapSlot::Albedo] =
          "asset://texture";
    }
    const auto result = prepareTerrainGpuFieldData3D(field, resolve, 16);
    assert(result.compatible && result.mapped == mapped);
    assert(result.paletteEntries == 1);
    const auto &color = result.palette.front();
    // Decode the product: sRGB 0.5 * 0.5 becomes linear 0.0508761,
    // rather than multiplying two independently decoded 0.214041 channels.
    assert(std::abs(color[0] - 0.050876088F) < 1e-7F);
    assert(std::abs(color[1] - 0.0007739938F) < 1e-9F);
    assert(std::abs(color[2] - 0.21404114F) < 1e-7F);
    assert(color[3] == 1.F);
    assert(field.biomeColors[0].r == 0.5F);
    assert(binding.baseColor[0] == 0.5F);
  }
  assert(std::abs(decodeSrgbChannel(0.04045) - 0.00313080495) < 1e-11);
  assert(std::abs(encodeSrgbChannel(0.0031308) - 0.040449936) < 1e-11);
  assert(std::abs(encodeSrgbChannel(0.21404114048223255) - 0.5) < 1e-12);
  assert(decodeSrgbChannel(0) == 0 && decodeSrgbChannel(1) == 1);
  assert(encodeSrgbChannel(0) == 0);
}

void overridesAndDeduplicationAreIndependentOfRoles() {
  auto field = fixture();
  field.biomeMaterials = {"asset://override", "asset://override"};
  auto override = material({0.2F, 0.4F, 0.6F, 1});
  int calls = 0;
  const auto resolve = [&](std::string_view id) -> const MaterialBinding * {
    assert(id == "asset://override");
    ++calls;
    return &override;
  };
  const auto result = prepareTerrainGpuFieldData3D(field, resolve, 16);
  assert(result.compatible && result.paletteEntries == 2 && calls == 1);
  assert(result.materialIndices[0] == result.materialIndices[1]);
  assert(result.materialIndices[2] == result.materialIndices[3]);

  field.biomeColors[1] = field.biomeColors[0];
  const auto deduplicated = prepareTerrainGpuFieldData3D(field, resolve, 16);
  assert(deduplicated.compatible && deduplicated.paletteEntries == 1);
  for (const auto bytes : deduplicated.materialIndices) {
    assert(decode(bytes) == 0);
  }
}

void intrinsicUniformMetadataIsSupported() {
  auto field = fixture();
  field.biomeMaterials = {"asset://solid", "asset://solid"};
  auto solid = material({0.5F, 0.6F, 0.7F, 1});
  solid.parameters.push_back({.uniform = {.index = 4},
                              .value = {0.5F, 0.6F, 0.7F, 1},
                              .name = "base_color"});
  solid.parameters.push_back(
      {.uniform = {.index = 5}, .value = {0.8F, 0, 0, 0}, .name = "roughness"});
  for (const auto &parameter : solid.parameters) {
    solid.uniforms.push_back(
        {.handle = parameter.uniform, .values = parameter.value});
  }
  const auto resolve = [&](std::string_view) { return &solid; };
  assert(prepareTerrainGpuFieldData3D(field, resolve, 16).compatible);
}

void expectFallback(const HeightField &field, const MaterialBinding &binding,
                    std::string_view reason) {
  const auto resolve = [&](std::string_view) { return &binding; };
  const auto result = prepareTerrainGpuFieldData3D(field, resolve, 16);
  assert(!result.compatible);
  assert(result.fallbackReason.find(reason) != std::string::npos);
  assert(result.heights.empty() && result.normals.empty());
  assert(result.materialIndices.empty() && result.palette.empty());
}

void mappedPalettesRetainMaterialIdentityAndSurfaceControls() {
  auto field = fixture();
  field.biomeTextureScales = {2, 3};
  auto earth = material({0.2F, 0.4F, 0.6F, 1});
  earth.albedoTexture = "asset://textures/earth";
  earth.usesPbrSurface = true;
  earth.terrainWrap = TextureWrap::Mirror;
  earth.surface.maps = {
      {demi::assets::MaterialMapSlot::Albedo, earth.albedoTexture},
      {demi::assets::MaterialMapSlot::Normal, "asset://textures/normal"},
      {demi::assets::MaterialMapSlot::DetailNormal, "asset://textures/detail"},
      {demi::assets::MaterialMapSlot::Height, "asset://textures/height"}};
  earth.surface.mapping = demi::assets::MaterialMapping::Triplanar;
  earth.surface.tiling = 4;
  earth.surface.normalMapYDown = true;
  earth.surface.normalStrength = 2;
  earth.surface.detailStrength = 0.3F;
  earth.surface.heightBlendStrength = 0.75F;
  earth.surface.macroVariationStrength = 0.6F;
  earth.surface.macroVariationScale = 0.04F;
  auto stone = material({0.8F, 0.7F, 0.5F, 1});
  stone.terrainWrap = TextureWrap::Clamp;
  stone.unlit = true;
  stone.surface.macroVariationStrength = 0.4F;
  stone.surface.normalStrength = 3;
  stone.surface.detailStrength = 0.9F;
  const auto resolve = [&](std::string_view id) {
    return id == "asset://earth" ? &earth : &stone;
  };
  const auto result = prepareTerrainGpuFieldData3D(field, resolve, 16);
  assert(result.compatible && result.mapped);
  assert(result.materialIds ==
         std::vector<std::string>({"", "asset://earth", "asset://stone"}));
  for (std::size_t sample = 0; sample < field.heights.size(); ++sample) {
    const auto biome = field.biomeIndices[sample];
    const bool isEarth = sample % 2 == 0;
    const auto &material = isEarth ? earth : stone;
    const auto entry = decode(result.materialIndices[sample]);
    const auto offset = entry * 5;
    const auto tint = field.biomeColors[biome];
    assert(result.palette[offset] ==
           (std::array<float, 4>{static_cast<float>(decodeSrgbChannel(
                                     tint.r * material.baseColor[0])),
                                 static_cast<float>(decodeSrgbChannel(
                                     tint.g * material.baseColor[1])),
                                 static_cast<float>(decodeSrgbChannel(
                                     tint.b * material.baseColor[2])),
                                 1}));
    assert(result.palette[offset + 1][2] == (isEarth ? 0.F : 1.F));
    assert(result.palette[offset + 2][0] == (isEarth ? 1.F : 2.F));
    assert(result.palette[offset + 2][1] ==
           material.surface.tiling * field.biomeTextureScale(biome));
    assert(result.palette[offset + 2][2] == (isEarth ? 1.F : 0.F));
    assert(result.palette[offset + 2][3] == (isEarth ? -1.F : 1.F));
    assert(result.palette[offset + 3][0] == (isEarth ? 2.F : 0.F));
    assert(result.palette[offset + 3][1] == (isEarth ? 0.3F : 0.F));
    assert(result.palette[offset + 3][2] == (isEarth ? 0.75F : 0.F));
    assert(result.palette[offset + 3][3] ==
           material.surface.macroVariationStrength);
    assert(result.palette[offset + 4][0] ==
           material.surface.macroVariationScale);
    assert(result.palette[offset + 4][1] == (isEarth ? 2.F : 1.F));
  }
  // Builtin projection and map metadata are admitted to the field path;
  // actual map-array preparation remains a separate transactional request.
  earth.surface.maps.clear();
  earth.albedoTexture.clear();
  earth.surface.macroVariationStrength = 0;
  stone.surface.macroVariationStrength = 0;
  const auto solid = prepareTerrainGpuFieldData3D(field, resolve, 16);
  assert(solid.compatible && !solid.mapped);
}

void unsupportedMaterialsNeverBecomeDefaultPaletteEntries() {
  const auto field = fixture();
  const auto original = material({1, 1, 1, 1});
  const std::array<
      std::pair<std::function<void(MaterialBinding &)>, std::string_view>, 8>
      cases{{
          {[](auto &m) { m.program = {.index = 0}; }, "custom shader"},
          {[](auto &m) { m.opacity = 0.5F; }, "transparency"},
          {[](auto &m) { m.alphaCutoff = 0.5F; }, "cutout"},
          {[](auto &m) { m.state.blend = BlendMode::Alpha; }, "raster/depth"},
          {[](auto &m) { m.state.depthTest = DepthTest::Disabled; },
           "raster/depth"},
          {[](auto &m) { m.state.writeDepth = false; }, "raster/depth"},
          {[](auto &m) { m.parameters.push_back({.name = "u_tint"}); },
           "shader-specific"},
          {[](auto &m) { m.uniforms.push_back({.handle = {.index = 3}}); },
           "unclassified"},
      }};
  for (const auto &[mutate, reason] : cases) {
    auto changed = original;
    mutate(changed);
    expectFallback(field, changed, reason);
  }
  auto translucentField = field;
  translucentField.biomeColors[0].a = 0.5F;
  expectFallback(translucentField, original, "tint requires transparency");
  const auto missing = prepareTerrainGpuFieldData3D(field, {}, 16);
  assert(!missing.compatible &&
         missing.fallbackReason.find("unavailable") != std::string::npos);
}

void mixedCullModesUsePerPixelFlagsAndOtherStatesStillFallBack() {
  auto field = fixture();
  field.biomeIndices.assign(6, 0);
  auto first = material({1, 1, 1, 1});
  auto second = first;
  second.state.cull = CullMode::None;
  const auto resolve = [&](std::string_view id) {
    return id == "asset://earth" ? &first : &second;
  };
  const auto result = prepareTerrainGpuFieldData3D(field, resolve, 16);
  assert(result.compatible && result.state.cull == CullMode::None);
  assert(result.requiresFragmentCulling);
  assert(result.paletteEntries == 2);
  for (std::size_t sample = 0; sample < 6; ++sample) {
    const auto entry = decode(result.materialIndices[sample]);
    assert(result.palette[entry * 2 + 1][3] == (sample % 2 == 0 ? 1.0F : 0.0F));
  }
  // Matching colors with different culling must remain distinct palette
  // entries.
  assert(result.materialIndices[0] != result.materialIndices[1]);

  second.state.cull = CullMode::Clockwise;
  const auto homogeneous = prepareTerrainGpuFieldData3D(field, resolve, 16);
  assert(homogeneous.compatible &&
         homogeneous.state.cull == CullMode::Clockwise);
  assert(homogeneous.paletteEntries == 1);
  assert(!homogeneous.requiresFragmentCulling);

  first.state.cull = CullMode::None;
  second.state.cull = CullMode::None;
  const auto doubleSided = prepareTerrainGpuFieldData3D(field, resolve, 16);
  assert(doubleSided.compatible && doubleSided.state.cull == CullMode::None);
  assert(!doubleSided.requiresFragmentCulling);
  second.state.cull = CullMode::Clockwise;
  const auto reversedMixed = prepareTerrainGpuFieldData3D(field, resolve, 16);
  assert(reversedMixed.compatible && reversedMixed.requiresFragmentCulling);

  second.state.depthTest = DepthTest::Disabled;
  const auto incompatibleDepth =
      prepareTerrainGpuFieldData3D(field, resolve, 16);
  assert(!incompatibleDepth.compatible &&
         !incompatibleDepth.fallbackReason.empty());
  second = first;
  second.state.blend = BlendMode::Alpha;
  const auto incompatibleBlend =
      prepareTerrainGpuFieldData3D(field, resolve, 16);
  assert(!incompatibleBlend.compatible &&
         !incompatibleBlend.fallbackReason.empty());
}

HeightField manyBiomes() {
  HeightField field;
  field.cellsX = 299;
  field.cellsZ = 1;
  field.size = {299, 1};
  field.heights.resize(600);
  field.normals.resize(600);
  field.biomeIndices.resize(600);
  for (std::size_t biome = 0; biome < 300; ++biome) {
    field.biomeIds.push_back("biome/" + std::to_string(biome));
    field.biomeColors.push_back({float(biome) / 300.0F, 0.5F, 0.5F, 1});
  }
  for (std::size_t sample = 0; sample < 600; ++sample) {
    field.normals.set(sample, {0, 1, 0});
    field.biomeIndices.set(sample, sample % 300);
  }
  return field;
}

void paletteHasNoFixedEightBitEntryLimit() {
  const auto field = manyBiomes();
  const auto result = prepareTerrainGpuFieldData3D(field, {}, 512);
  assert(result.compatible && result.paletteEntries == 300);
  assert(result.paletteWidth == 512 && result.paletteHeight == 2);
  assert(result.palette.size() == 1024);
  assert(decode(result.materialIndices[299]) == 299);
  const std::array<std::uint8_t, 4> expected{43, 1, 0, 0};
  assert(result.materialIndices[299] == expected);
  for (std::size_t texel = result.paletteEntries * 2;
       texel < result.palette.size(); ++texel) {
    assert((result.palette[texel] == std::array<float, 4>{}));
  }
}

void actualDeviceLimitsAndInvalidSamplesAreReported() {
  auto field = fixture();
  const auto binding = material({1, 1, 1, 1});
  const auto resolve = [&](std::string_view) { return &binding; };
  const auto tooSmall = prepareTerrainGpuFieldData3D(field, resolve, 2);
  assert(!tooSmall.compatible && tooSmall.fallbackReason.find(
                                     "device dimensions") != std::string::npos);
  field.heights.set(0, std::numeric_limits<float>::infinity());
  expectFallback(field, binding, "non-finite");
  field = fixture();
  field.normals.set(0, {0, std::numeric_limits<float>::quiet_NaN(), 0});
  expectFallback(field, binding, "non-finite");
  field = fixture();
  field.biomeIndices.set(0, 999);
  expectFallback(field, binding, "unknown biome");

  field = fixture();
  field.surfaceMaterials.reset();
  field.biomeIds.clear();
  field.biomeColors.clear();
  for (std::size_t index = 0; index < 6; ++index) {
    field.biomeIds.push_back(std::to_string(index));
    field.biomeColors.push_back({float(index) / 6.0F, 0.5F, 0.5F, 1});
    field.biomeIndices.set(index, index);
  }
  const auto paletteTooLarge = prepareTerrainGpuFieldData3D(field, {}, 3);
  assert(!paletteTooLarge.compatible &&
         paletteTooLarge.fallbackReason.find("palette exceeds") !=
             std::string::npos);
}

} // namespace

int main() {
  sharpMaterialsFollowCellTrianglesWithoutInterpolatingIds();
  samplesAndPaletteRetainCanonicalValues();
  paletteDecodesCombinedSrgbTintForSolidAndMappedMaterials();
  overridesAndDeduplicationAreIndependentOfRoles();
  intrinsicUniformMetadataIsSupported();
  unsupportedMaterialsNeverBecomeDefaultPaletteEntries();
  mappedPalettesRetainMaterialIdentityAndSurfaceControls();
  mixedCullModesUsePerPixelFlagsAndOtherStatesStillFallBack();
  paletteHasNoFixedEightBitEntryLimit();
  actualDeviceLimitsAndInvalidSamplesAreReported();
  std::cout << "Terrain GPU field data checks passed\n";
}
