#include "demi/runtime/render/backend/BgfxDrawBindings.h"

#include <array>

namespace demi::runtime::render {

bool BgfxDrawBindings::resolveUniforms(std::span<const DrawUniformValue> values,
                                       BgfxResourceLookup &resources,
                                       std::string &error,
                                       const char *drawName) {
  uniforms_.clear();
  uniforms_.reserve(values.size());
  for (const auto &value : values) {
    const auto handle = resources.bgfxUniform(value.handle);
    if (!bgfx::isValid(handle) || value.values.empty() || value.count == 0) {
      uniforms_.clear();
      error = std::string(drawName) + " references an invalid uniform value.";
      return false;
    }
    uniforms_.push_back({handle, value.values, value.count});
  }
  return true;
}

bool BgfxDrawBindings::resolveTextures(
    std::span<const DrawTextureBinding> textures, BgfxResourceLookup &resources,
    std::uint16_t maximumSamplers, std::string &error) {
  textures_.clear();
  textures_.reserve(textures.size());
  std::array<bool, 256> occupiedStages{};
  for (const auto &binding : textures) {
    const bool validStage = binding.stage != 0 &&
                            binding.stage < maximumSamplers &&
                            !occupiedStages[binding.stage];
    const auto texture = resources.bgfxTexture(binding.texture);
    const auto sampler = resources.bgfxSampler(binding.sampler);
    if (!validStage || !bgfx::isValid(texture) || !bgfx::isValid(sampler)) {
      textures_.clear();
      error = "Invalid additional texture binding";
      return false;
    }
    occupiedStages[binding.stage] = true;
    textures_.push_back({binding.stage, texture, sampler});
  }
  return true;
}
} // namespace demi::runtime::render
