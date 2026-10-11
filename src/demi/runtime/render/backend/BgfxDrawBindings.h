#pragma once

#include "demi/runtime/render/backend/BgfxResourceLookup.h"
#include "demi/runtime/render/backend/RenderCommands.h"

#include <span>
#include <string>
#include <vector>

namespace demi::runtime::render {

struct BgfxResolvedUniform {
  bgfx::UniformHandle handle;
  std::span<const float> values;
  std::uint16_t count = 1;
};

struct BgfxResolvedTexture {
  std::uint8_t stage = 0;
  bgfx::TextureHandle texture;
  bgfx::UniformHandle sampler;
};

// Scratch for one synchronous submission. Handles are resolved afresh for each
// draw, so resource destruction/reuse cannot leave a persistent native cache.
class BgfxDrawBindings {
public:
  bool resolveUniforms(std::span<const DrawUniformValue> values,
                       BgfxResourceLookup &resources, std::string &error,
                       const char *drawName);
  bool resolveTextures(std::span<const DrawTextureBinding> textures,
                       BgfxResourceLookup &resources,
                       std::uint16_t maximumSamplers, std::string &error);

  std::span<const BgfxResolvedUniform> uniforms() const { return uniforms_; }
  std::span<const BgfxResolvedTexture> textures() const { return textures_; }

private:
  std::vector<BgfxResolvedUniform> uniforms_;
  std::vector<BgfxResolvedTexture> textures_;
};
} // namespace demi::runtime::render
