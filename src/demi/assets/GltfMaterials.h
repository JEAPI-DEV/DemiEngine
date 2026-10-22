#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace demi::assets {

enum class GltfMapRole {
  Albedo,
  Normal,
  MetallicRoughness,
  Occlusion,
  Emissive
};
enum class GltfWrap { Repeat, Clamp, Mirror };
enum class GltfFilter {
  Nearest,
  Linear,
  NearestMipmapNearest,
  LinearMipmapNearest,
  NearestMipmapLinear,
  LinearMipmapLinear
};
enum class GltfAlphaMode { Opaque, Mask, Blend };

struct GltfMaterialTexture {
  std::shared_ptr<const std::vector<std::byte>> encoded;
  GltfWrap wrapU = GltfWrap::Repeat;
  GltfWrap wrapV = GltfWrap::Repeat;
  GltfFilter minFilter = GltfFilter::LinearMipmapLinear;
  GltfFilter magFilter = GltfFilter::Linear;
};

struct GltfMaterial3D {
  std::string name;
  runtime::Color baseColor{1, 1, 1, 1};
  float metallic = 1;
  float roughness = 1;
  float normalStrength = 1;
  float occlusionStrength = 1;
  runtime::Vec3 emissive;
  GltfAlphaMode alphaMode = GltfAlphaMode::Opaque;
  float alphaCutoff = 0.5F;
  bool doubleSided = false;
  bool unlit = false;
  std::map<GltfMapRole, GltfMaterialTexture> textures;
  std::vector<std::string> diagnostics;
};

struct GltfMaterialSection3D {
  std::size_t material = 0;
  std::uint32_t firstIndex = 0;
  std::uint32_t indexCount = 0;
};

// Imports source material values without creating assets or renderer handles.
// A final default material covers primitives with no material assignment.
[[nodiscard]] bool loadGltfMaterials(const std::filesystem::path &source,
                                     std::vector<GltfMaterial3D> &materials,
                                     std::string &error);

} // namespace demi::assets
