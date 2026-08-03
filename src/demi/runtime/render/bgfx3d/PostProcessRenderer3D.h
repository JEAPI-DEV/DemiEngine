#pragma once

#include "demi/runtime/render/backend/RenderCommands.h"
#include "demi/runtime/render/bgfx3d/BgfxCameraFrame3D.h"

#include <cstdint>
#include <string>

namespace demi::runtime::render {

class PostProcessRenderer3D {
public:
  PostProcessRenderer3D(GpuResources &resources, RenderCommands &commands);
  ~PostProcessRenderer3D();

  PostProcessRenderer3D(const PostProcessRenderer3D &) = delete;
  PostProcessRenderer3D &operator=(const PostProcessRenderer3D &) = delete;

  [[nodiscard]] bool initialize(std::string &error);
  void shutdown();
  [[nodiscard]] const RenderTargetHandles &
  scratchTarget(std::uint16_t width, std::uint16_t height, std::string &error);
  [[nodiscard]] bool present(const BgfxCameraFrame3D &frame,
                             TextureHandle source,
                             FrameBufferHandle destination, std::string &error);

private:
  void destroyScratchTarget();

  GpuResources &resources_;
  RenderCommands &commands_;
  ProgramHandle program_;
  SamplerHandle sampler_;
  UniformHandle colorAdjustUniform_;
  UniformHandle tintUniform_;
  UniformHandle bloomFadeUniform_;
  UniformHandle fadeColorUniform_;
  RenderTargetHandles scratch_;
  std::uint16_t scratchWidth_ = 0;
  std::uint16_t scratchHeight_ = 0;
};

[[nodiscard]] bool hasPostProcessEffects(
    const std::optional<PostProcessStackComponent> &postProcess);

} // namespace demi::runtime::render
