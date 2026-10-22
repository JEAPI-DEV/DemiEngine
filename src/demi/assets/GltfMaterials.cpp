#include "demi/assets/GltfMaterials.h"
#include "demi/assets/GltfMaterialImport.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <set>
#include <string_view>

namespace demi::assets {
namespace {

GltfWrap wrapMode(cgltf_wrap_mode mode) {
  switch (mode) {
  case cgltf_wrap_mode_clamp_to_edge:
    return GltfWrap::Clamp;
  case cgltf_wrap_mode_mirrored_repeat:
    return GltfWrap::Mirror;
  default:
    return GltfWrap::Repeat;
  }
}

GltfFilter filterMode(cgltf_filter_type mode, GltfFilter fallback) {
  switch (mode) {
  case cgltf_filter_type_nearest:
    return GltfFilter::Nearest;
  case cgltf_filter_type_linear:
    return GltfFilter::Linear;
  case cgltf_filter_type_nearest_mipmap_nearest:
    return GltfFilter::NearestMipmapNearest;
  case cgltf_filter_type_linear_mipmap_nearest:
    return GltfFilter::LinearMipmapNearest;
  case cgltf_filter_type_nearest_mipmap_linear:
    return GltfFilter::NearestMipmapLinear;
  case cgltf_filter_type_linear_mipmap_linear:
    return GltfFilter::LinearMipmapLinear;
  default:
    return fallback;
  }
}

std::optional<std::vector<std::byte>>
readImage(const cgltf_image &image, const std::filesystem::path &source,
          std::string &error) {
  if (image.buffer_view != nullptr) {
    const auto *data = cgltf_buffer_view_data(image.buffer_view);
    if (data != nullptr) {
      const auto *begin = reinterpret_cast<const std::byte *>(data);
      return std::vector<std::byte>(begin, begin + image.buffer_view->size);
    }
  } else if (image.uri != nullptr) {
    const std::string_view uri(image.uri);
    if (uri.starts_with("data:")) {
      const auto separator = uri.find(",");
      if (separator != std::string_view::npos &&
          uri.substr(0, separator).ends_with(";base64")) {
        const auto payload = uri.substr(separator + 1);
        if (payload.size() < 4 || payload.size() % 4 != 0) {
          error = "Invalid base64 glTF image payload.";
          return std::nullopt;
        }
        std::size_t size = payload.size() / 4 * 3;
        if (payload.ends_with("==")) {
          size -= 2;
        } else if (payload.ends_with("=")) {
          --size;
        }
        cgltf_options options{};
        void *data = nullptr;
        if (size != 0 &&
            cgltf_load_buffer_base64(&options, size, payload.data(), &data) ==
                cgltf_result_success) {
          const auto *begin = static_cast<const std::byte *>(data);
          const std::unique_ptr<void, decltype(&std::free)> allocation(
              data, &std::free);
          return std::vector<std::byte>(begin, begin + size);
        }
      }
    } else {
      std::string decoded(uri);
      decoded.resize(cgltf_decode_uri(decoded.data()));
      std::ifstream input(source.parent_path() / decoded, std::ios::binary);
      if (input) {
        const std::vector<char> contents(
            (std::istreambuf_iterator<char>(input)), {});
        std::vector<std::byte> result(contents.size());
        std::transform(
            contents.begin(), contents.end(), result.begin(), [](char value) {
              return static_cast<std::byte>(static_cast<unsigned char>(value));
            });
        return result;
      }
    }
  }
  error = "Could not read glTF material image in " + source.string();
  return std::nullopt;
}

} // namespace

bool importGltfMaterials(const cgltf_data &document,
                         const std::filesystem::path &source,
                         std::vector<GltfMaterial3D> &materials,
                         std::string &error) {
  std::map<const cgltf_image *, std::shared_ptr<const std::vector<std::byte>>>
      images;
  std::set<const cgltf_material *> used;
  for (cgltf_size node = 0; node < document.nodes_count; ++node) {
    if (const auto *mesh = document.nodes[node].mesh) {
      for (cgltf_size index = 0; index < mesh->primitives_count; ++index) {
        if (mesh->primitives[index].type == cgltf_primitive_type_triangles) {
          used.insert(mesh->primitives[index].material);
        }
      }
    }
  }
  std::vector<GltfMaterial3D> imported;
  imported.reserve(document.materials_count + 1);
  for (cgltf_size index = 0; index < document.materials_count; ++index) {
    const auto &original = document.materials[index];
    GltfMaterial3D material;
    material.name = original.name == nullptr ? std::string{} : original.name;
    material.doubleSided = original.double_sided;
    material.unlit = original.unlit;
    material.alphaMode =
        original.alpha_mode == cgltf_alpha_mode_blend  ? GltfAlphaMode::Blend
        : original.alpha_mode == cgltf_alpha_mode_mask ? GltfAlphaMode::Mask
                                                       : GltfAlphaMode::Opaque;
    material.alphaCutoff = original.alpha_cutoff;
    if (original.has_pbr_metallic_roughness) {
      const auto &pbr = original.pbr_metallic_roughness;
      material.baseColor = {pbr.base_color_factor[0], pbr.base_color_factor[1],
                            pbr.base_color_factor[2], pbr.base_color_factor[3]};
      material.metallic = pbr.metallic_factor;
      material.roughness = pbr.roughness_factor;
    }
    material.normalStrength = original.normal_texture.scale;
    material.occlusionStrength = original.occlusion_texture.scale;
    const float emissiveStrength =
        original.has_emissive_strength
            ? original.emissive_strength.emissive_strength
            : 1;
    material.emissive = {original.emissive_factor[0] * emissiveStrength,
                         original.emissive_factor[1] * emissiveStrength,
                         original.emissive_factor[2] * emissiveStrength};
    const auto diagnose = [&](bool enabled, const char *feature) {
      if (enabled && used.contains(&original)) {
        material.diagnostics.push_back(
            "Material " + material.name + " uses unsupported glTF " + feature +
            "; only its core metallic-roughness surface is imported.");
      }
    };
    diagnose(original.has_pbr_specular_glossiness, "specular-glossiness");
    diagnose(original.has_clearcoat, "clearcoat");
    diagnose(original.has_transmission, "transmission");
    diagnose(original.has_volume, "volume");
    diagnose(original.has_ior, "IOR");
    diagnose(original.has_specular, "specular");
    diagnose(original.has_sheen, "sheen");
    diagnose(original.has_iridescence, "iridescence");
    diagnose(original.has_diffuse_transmission, "diffuse transmission");
    diagnose(original.has_anisotropy, "anisotropy");
    diagnose(original.has_dispersion, "dispersion");
    if (!used.contains(&original)) {
      imported.push_back(std::move(material));
      continue;
    }
    const auto texture = [&](GltfMapRole role, const cgltf_texture_view &view) {
      if (view.texture == nullptr) {
        return true;
      }
      const auto &transform = view.transform;
      const int texcoord = view.has_transform && transform.has_texcoord
                               ? transform.texcoord
                               : view.texcoord;
      const bool transformed =
          view.has_transform &&
          (transform.offset[0] != 0 || transform.offset[1] != 0 ||
           transform.rotation != 0 || transform.scale[0] != 1 ||
           transform.scale[1] != 1);
      if (texcoord != 0 || transformed) {
        error = "glTF material " + material.name +
                " requires an unsupported UV set or texture transform.";
        return false;
      }
      const auto *image = view.texture->image;
      if (image == nullptr) {
        error = "glTF material " + material.name + " has no supported image.";
        return false;
      }
      auto found = images.find(image);
      if (found == images.end()) {
        auto encoded = readImage(*image, source, error);
        if (!encoded) {
          return false;
        }
        found =
            images
                .emplace(image, std::make_shared<const std::vector<std::byte>>(
                                    std::move(*encoded)))
                .first;
      }
      GltfMaterialTexture entry;
      entry.encoded = found->second;
      if (const auto *sampler = view.texture->sampler) {
        entry.wrapU = wrapMode(sampler->wrap_s);
        entry.wrapV = wrapMode(sampler->wrap_t);
        entry.minFilter = filterMode(sampler->min_filter, entry.minFilter);
        entry.magFilter = filterMode(sampler->mag_filter, entry.magFilter);
      }
      material.textures.emplace(role, std::move(entry));
      return true;
    };
    if (!texture(GltfMapRole::Albedo,
                 original.pbr_metallic_roughness.base_color_texture) ||
        !texture(GltfMapRole::MetallicRoughness,
                 original.pbr_metallic_roughness.metallic_roughness_texture) ||
        !texture(GltfMapRole::Normal, original.normal_texture) ||
        !texture(GltfMapRole::Occlusion, original.occlusion_texture) ||
        !texture(GltfMapRole::Emissive, original.emissive_texture)) {
      return false;
    }
    imported.push_back(std::move(material));
  }
  imported.emplace_back();
  materials = std::move(imported);
  error.clear();
  return true;
}

bool loadGltfMaterials(const std::filesystem::path &source,
                       std::vector<GltfMaterial3D> &materials,
                       std::string &error) {
  auto document = gltf::parseDocument(source, error);
  if (!document || !gltf::loadDocumentBuffers(*document, source, error)) {
    return false;
  }
  return importGltfMaterials(*document, source, materials, error);
}

} // namespace demi::assets
