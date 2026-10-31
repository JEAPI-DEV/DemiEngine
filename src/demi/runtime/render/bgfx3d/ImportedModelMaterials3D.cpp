#include "demi/runtime/render/bgfx3d/ImportedModelMaterials3D.h"

#include "demi/runtime/render/backend/RenderAssetLoading.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace demi::runtime::render {
namespace {

using Slot = assets::MaterialMapSlot;
using Role = assets::GltfMapRole;
using Filter = assets::GltfFilter;

float linearToSrgb(float value) {
  if (value >= 1.0F) {
    return 1.0F;
  }
  return value <= 0.0031308F ? value * 12.92F
                             : 1.055F * std::pow(value, 1.0F / 2.4F) - 0.055F;
}
float srgbToLinear(float value) {
  return value <= 0.04045F ? value / 12.92F
                           : std::pow((value + 0.055F) / 1.055F, 2.4F);
}
std::byte byteValue(float value) {
  return static_cast<std::byte>(static_cast<unsigned char>(
      std::lround(std::clamp(value, 0.0F, 1.0F) * 255)));
}
TextureWrap wrap(assets::GltfWrap value) {
  switch (value) {
  case assets::GltfWrap::Clamp:
    return TextureWrap::Clamp;
  case assets::GltfWrap::Mirror:
    return TextureWrap::Mirror;
  default:
    return TextureWrap::Repeat;
  }
}
ImageMipSemantic semanticFor(Slot slot) {
  if (slot == Slot::Albedo || slot == Slot::Emissive) {
    return ImageMipSemantic::Color;
  }
  return slot == Slot::Normal ? ImageMipSemantic::Normal
                              : ImageMipSemantic::Linear;
}
Role roleFor(Slot slot) {
  switch (slot) {
  case Slot::Normal:
    return Role::Normal;
  case Slot::Roughness:
  case Slot::Metallic:
    return Role::MetallicRoughness;
  case Slot::AmbientOcclusion:
    return Role::Occlusion;
  case Slot::Emissive:
    return Role::Emissive;
  default:
    return Role::Albedo;
  }
}

TextureSampling2D samplingFor(const AssetManifest &asset,
                              const assets::GltfMaterialTexture *source,
                              ImageMipSemantic semantic) {
  TextureSampling2D sampling;
  sampling.wrap = source ? wrap(source->wrapU) : TextureWrap::Repeat;
  sampling.wrapV = source ? wrap(source->wrapV) : TextureWrap::Repeat;
  sampling.mipSemantic = semantic;
  const auto minimum = source ? source->minFilter : Filter::LinearMipmapLinear;
  sampling.minFilter = minimum == Filter::Nearest ||
                               minimum == Filter::NearestMipmapNearest ||
                               minimum == Filter::NearestMipmapLinear
                           ? TextureFilter::Nearest
                           : TextureFilter::Linear;
  sampling.magFilter = source && source->magFilter == Filter::Nearest
                           ? TextureFilter::Nearest
                           : TextureFilter::Linear;
  sampling.mipFilter = minimum == Filter::NearestMipmapNearest ||
                               minimum == Filter::LinearMipmapNearest
                           ? TextureFilter::Nearest
                           : TextureFilter::Linear;
  sampling.mipmaps = minimum != Filter::Nearest && minimum != Filter::Linear;
  const auto settings =
      nlohmann::json::parse(asset.settingsJson, nullptr, false);
  if (settings.is_object()) {
    const auto authored = textureSampling2D(asset);
    if (settings.contains("wrap")) {
      sampling.wrap = authored.wrap;
      sampling.wrapV = authored.wrap;
    }
    if (settings.contains("filter")) {
      sampling.minFilter = authored.filter;
      sampling.magFilter = authored.filter;
      sampling.mipFilter = authored.filter;
    }
    if (settings.contains("mipmaps")) {
      sampling.mipmaps = authored.mipmaps;
    }
  }
  return sampling;
}

bool prepareMap(const AssetManifest &asset,
                const assets::GltfMaterial3D &material, Slot slot,
                ImageMipSemantic semantic, PreparedTexture2D &output,
                std::string &error, std::stop_token stop) {
  const auto found = material.textures.find(roleFor(slot));
  const auto *source =
      found == material.textures.end() ? nullptr : &found->second;
  ImageData2D image;
  if (slot == Slot::Albedo && asset.texturePath) {
    if (!decodeImage2D(readRenderAssetBytes(*asset.texturePath), image,
                       error)) {
      return false;
    }
  } else if (source) {
    if (!source->encoded || !decodeImage2D(*source->encoded, image, error)) {
      return false;
    }
  } else if (slot == Slot::Emissive) {
    image = {.width = 1,
             .height = 1,
             .rgba = {std::byte{255}, std::byte{255}, std::byte{255},
                      std::byte{255}}};
  } else {
    error = "Imported material texture source is unavailable.";
    return false;
  }
  for (std::size_t index = 0; index < image.rgba.size(); index += 4) {
    if (slot == Slot::Roughness || slot == Slot::Metallic ||
        slot == Slot::AmbientOcclusion) {
      const auto channel = slot == Slot::Roughness  ? 1U
                           : slot == Slot::Metallic ? 2U
                                                    : 0U;
      const float original =
          std::to_integer<unsigned char>(image.rgba[index + channel]) / 255.0F;
      const float value = slot == Slot::Roughness
                              ? original * material.roughness
                          : slot == Slot::Metallic
                              ? original * material.metallic
                              : 1 - material.occlusionStrength * (1 - original);
      image.rgba[index] = image.rgba[index + 1] = image.rgba[index + 2] =
          byteValue(value);
    } else if (slot == Slot::Emissive) {
      const std::array factors{material.emissive.x, material.emissive.y,
                               material.emissive.z};
      for (std::size_t channel = 0; channel < 3; ++channel) {
        const float original =
            std::to_integer<unsigned char>(image.rgba[index + channel]) /
            255.0F;
        image.rgba[index + channel] =
            byteValue(linearToSrgb(srgbToLinear(original) * factors[channel]));
      }
    }
    if (slot == Slot::Albedo &&
        material.alphaMode == assets::GltfAlphaMode::Opaque) {
      image.rgba[index + 3] = std::byte{255};
    }
  }
  auto sampling = samplingFor(asset, source, semantic);
  if (slot == Slot::Albedo && material.alphaMode == assets::GltfAlphaMode::Mask &&
      material.alphaCutoff > 0 && material.baseColor.a > 0) {
    const float cutoff = material.alphaCutoff / material.baseColor.a;
    if (cutoff <= 1) {
      sampling.alphaCoverageCutoff = cutoff;
    }
  }
  return prepareTexture2D(image, sampling, output, error, stop);
}

} // namespace

std::string importedMaterialId(std::string_view model, std::size_t material) {
  return std::string(model) + "#material/" + std::to_string(material);
}

bool prepareImportedMaterial(const AssetManifest &asset, std::size_t index,
                             const assets::GltfMaterial3D &material,
                             bool forceUnlit,
                             PreparedImportedMaterial3D &output,
                             std::string &error, std::stop_token stop) {
  PreparedImportedMaterial3D prepared;
  prepared.id = importedMaterialId(asset.id, index);
  prepared.sourceMaterial = index;
  auto &binding = prepared.binding;
  binding.unlit = forceUnlit || material.unlit;
  binding.metallic = material.metallic;
  binding.roughness = material.roughness;
  binding.baseColor = {linearToSrgb(material.baseColor.r),
                       linearToSrgb(material.baseColor.g),
                       linearToSrgb(material.baseColor.b),
                       material.alphaMode == assets::GltfAlphaMode::Opaque
                           ? 1.0F
                           : material.baseColor.a};
  binding.surface.baseColor = {binding.baseColor[0], binding.baseColor[1],
                               binding.baseColor[2], binding.baseColor[3]};
  binding.surface.metallic = material.metallic;
  binding.surface.roughness = material.roughness;
  binding.surface.normalStrength = material.normalStrength;
  binding.state.depthTest = DepthTest::Less;
  binding.state.cull =
      material.doubleSided ? CullMode::None : CullMode::Clockwise;
  binding.state.blend = material.alphaMode == assets::GltfAlphaMode::Blend
                            ? BlendMode::Alpha
                            : BlendMode::Opaque;
  binding.state.writeDepth = material.alphaMode != assets::GltfAlphaMode::Blend;
  binding.alphaCutoff = material.alphaMode == assets::GltfAlphaMode::Mask
                            ? material.alphaCutoff
                            : 0;
  if (material.emissive.x > 1 || material.emissive.y > 1 ||
      material.emissive.z > 1) {
    error = "Imported HDR emissive factors are not representable by the "
            "current material surface.";
    return false;
  }
  for (const auto slot :
       {Slot::Albedo, Slot::Normal, Slot::Roughness, Slot::Metallic,
        Slot::AmbientOcclusion, Slot::Emissive}) {
    const bool constantEmissive =
        slot == Slot::Emissive &&
        (material.emissive.x != 0 || material.emissive.y != 0 ||
         material.emissive.z != 0);
    const bool overrideAlbedo =
        slot == Slot::Albedo && asset.texturePath.has_value();
    if (!material.textures.contains(roleFor(slot)) && !constantEmissive &&
        !overrideAlbedo) {
      continue;
    }
    PreparedImportedTexture3D texture{
        .slot = slot,
        .id =
            prepared.id + "/" + std::string(assets::materialMapSlotName(slot))};
    if (!prepareMap(asset, material, slot, semanticFor(slot), texture.texture,
                    error, stop)) {
      return false;
    }
    binding.surface.maps.emplace(slot, texture.id);
    if (slot == Slot::Albedo) {
      binding.albedoTexture = texture.id;
    } else {
      binding.usesPbrSurface = true;
    }
    prepared.textures.push_back(std::move(texture));
  }
  output = std::move(prepared);
  error.clear();
  return true;
}

bool reloadImportedTexture(const AssetManifest &asset, std::size_t material,
                           Slot slot, ImageMipSemantic semantic,
                           PreparedTexture2D &output, std::string &error,
                           std::stop_token stop) {
  std::vector<assets::GltfMaterial3D> materials;
  if (!assets::loadGltfMaterials(asset.sourcePath, materials, error)) {
    return false;
  }
  if (material >= materials.size()) {
    error = "Imported material index is no longer present in its source.";
    return false;
  }
  return prepareMap(asset, materials[material], slot, semantic, output, error,
                    stop);
}

} // namespace demi::runtime::render
