#pragma once

#include "demi/runtime/render/backend/GpuResources.h"

#include <bgfx/bgfx.h>

namespace demi::runtime::render {

// Private adapter shared only by bgfx implementation files. It keeps native
// handle resolution out of the public renderer and gameplay APIs.
class BgfxResourceLookup {
public:
  virtual ~BgfxResourceLookup() = default;

  [[nodiscard]] virtual bgfx::TextureHandle
  bgfxTexture(TextureHandle handle) const = 0;
  [[nodiscard]] virtual bgfx::UniformHandle
  bgfxSampler(SamplerHandle handle) const = 0;
  [[nodiscard]] virtual bgfx::ProgramHandle
  bgfxProgram(ProgramHandle handle) const = 0;
  [[nodiscard]] virtual bgfx::FrameBufferHandle
  bgfxFrameBuffer(FrameBufferHandle handle) const = 0;
};

} // namespace demi::runtime::render
