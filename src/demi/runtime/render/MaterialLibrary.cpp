#include "demi/runtime/render/MaterialLibrary.h"

#include "demi/assets/RenderAsset.h"

#include <algorithm>
#include <ranges>

namespace demi::runtime::render {
namespace {

BlendMode blendMode(const std::string &value) {
  if (value == "additive")
    return BlendMode::Additive;
  if (value == "alpha")
    return BlendMode::Alpha;
  return BlendMode::Opaque;
}

CullMode cullMode(const std::string &value) {
  if (value == "front")
    return CullMode::CounterClockwise;
  if (value == "none")
    return CullMode::None;
  return CullMode::Clockwise;
}

std::string uniformName(const std::string &parameter) {
  return parameter.starts_with("u_") ? parameter : "u_" + parameter;
}

} // namespace

MaterialLibrary::MaterialLibrary(GpuResources &resources)
    : resources_(resources), shaders_(resources) {}

MaterialLibrary::~MaterialLibrary() { clear(); }

bool MaterialLibrary::load(const AssetRegistry &registry,
                           std::vector<std::string> &diagnostics) {
  clear();
  if (!shaders_.load(registry, diagnostics))
    return false;

  bool success = true;
  std::uint32_t nextUniformSet = 1;
  for (const AssetManifest &asset : registry.assets) {
    if (asset.type != "Material")
      continue;
    const auto descriptor = assets::loadMaterialAsset(asset.sourcePath);
    if (!descriptor) {
      diagnostics.push_back(asset.id + ": could not load material");
      success = false;
      continue;
    }
    MaterialBinding binding;
    binding.uniformSet = nextUniformSet++;
    binding.state = {.blend = blendMode(descriptor->renderState.blend),
                     .depthTest = descriptor->renderState.depthTest
                                      ? DepthTest::Less
                                      : DepthTest::Disabled,
                     .cull = cullMode(descriptor->renderState.cull),
                     .topology = PrimitiveTopology::Triangles,
                     .writeDepth = descriptor->renderState.depthWrite};
    if (const auto albedo = descriptor->textures.find("albedo");
        albedo != descriptor->textures.end())
      binding.albedoTexture = albedo->second;
    if (!descriptor->shader.starts_with("builtin://")) {
      binding.program = shaders_.find(descriptor->shader);
      if (!binding.program) {
        diagnostics.push_back(asset.id + ": shader " + descriptor->shader +
                              " was not loaded");
        success = false;
        continue;
      }
    }

    binding.parameters.reserve(descriptor->numbers.size() +
                               descriptor->colors.size());
    const auto append = [&](const std::string &name,
                            const std::array<float, 4> value) {
      std::string error;
      const UniformHandle uniform = resources_.createUniform(
          uniformName(name), UniformType::Vec4, 1, error);
      if (!uniform) {
        diagnostics.push_back(asset.id + ": " + error);
        return false;
      }
      binding.parameters.push_back({.uniform = uniform, .value = value});
      return true;
    };
    std::vector<std::string> names;
    names.reserve(descriptor->numbers.size());
    for (const auto &[name, unused] : descriptor->numbers)
      names.push_back(name);
    std::ranges::sort(names);
    bool bindingValid = true;
    for (const std::string &name : names) {
      const float value = descriptor->numbers.at(name);
      bindingValid = append(name, {value, 0.0F, 0.0F, 0.0F}) && bindingValid;
    }
    names.clear();
    names.reserve(descriptor->colors.size());
    for (const auto &[name, unused] : descriptor->colors)
      names.push_back(name);
    std::ranges::sort(names);
    for (const std::string &name : names) {
      const auto &color = descriptor->colors.at(name);
      bindingValid =
          append(name, {color.r, color.g, color.b, color.a}) && bindingValid;
    }
    if (!bindingValid) {
      for (const MaterialParameter &parameter : binding.parameters)
        resources_.destroy(parameter.uniform);
      success = false;
      continue;
    }
    binding.uniforms.reserve(binding.parameters.size());
    for (const MaterialParameter &parameter : binding.parameters)
      binding.uniforms.push_back(
          {.handle = parameter.uniform, .values = parameter.value});
    materials_.emplace(asset.id, std::move(binding));
  }
  return success;
}

void MaterialLibrary::clear() {
  for (auto &[unused, material] : materials_)
    for (const MaterialParameter &parameter : material.parameters)
      if (parameter.uniform)
        resources_.destroy(parameter.uniform);
  materials_.clear();
  shaders_.clear();
}

const MaterialBinding *MaterialLibrary::find(
    const std::string_view assetId) const {
  const auto found = materials_.find(std::string(assetId));
  return found == materials_.end() ? nullptr : &found->second;
}

} // namespace demi::runtime::render
