#pragma once

#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/QuadBatch.h"
#include "demi/runtime/render/backend/RenderCommands.h"

#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>

namespace demi::runtime::render {

struct Rect2D {
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;
};

struct TextureRegion2D {
  float u0 = 0.0F;
  float v0 = 0.0F;
  float u1 = 1.0F;
  float v1 = 1.0F;
};

struct NinePatch2D {
  // Destination margins in canvas units.
  float left = 0.0F;
  float top = 0.0F;
  float right = 0.0F;
  float bottom = 0.0F;
  // Absolute UV coordinates separating the fixed edges from the center.
  TextureRegion2D center;
};

struct Canvas2DStatistics {
  std::uint32_t quads = 0;
  std::uint32_t drawCalls = 0;
  std::uint32_t vertices = 0;
  std::uint32_t indices = 0;
  std::uint32_t triangles = 0;
};

// Backend-neutral immediate 2D canvas. It groups compatible commands and owns
// only the tiny fallback resources needed for solid-color drawing.
class Canvas2D {
public:
  Canvas2D(GpuResources &resources, RenderCommands &commands,
           std::size_t maxQuadsPerDraw = 16383);
  ~Canvas2D();

  Canvas2D(const Canvas2D &) = delete;
  Canvas2D &operator=(const Canvas2D &) = delete;

  [[nodiscard]] bool initialize(std::string &error);
  void shutdown();
  [[nodiscard]] bool begin(std::uint16_t viewId, std::uint16_t width,
                           std::uint16_t height, std::uint32_t clearRgba,
                           std::string &error, bool clear = true,
                           std::uint16_t x = 0, std::uint16_t y = 0,
                           FrameBufferHandle frameBuffer = {});

  [[nodiscard]] bool solid(const Rect2D &destination, std::uint32_t rgba,
                           BlendMode blend = BlendMode::Alpha,
                           ScissorRect scissor = {},
                           ProgramHandle program = {},
                           std::uint32_t uniformSet = 0);
  [[nodiscard]] bool image(TextureHandle texture, const Rect2D &destination,
                           const TextureRegion2D &source = {},
                           std::uint32_t rgba = 0xffffffffU,
                           BlendMode blend = BlendMode::Alpha,
                           ScissorRect scissor = {},
                           ProgramHandle program = {},
                           std::uint32_t uniformSet = 0);
  [[nodiscard]] bool imageTransformed(
      TextureHandle texture, float positionX, float positionY, float width,
      float height, float pivotX, float pivotY, float rotationRadians,
      const TextureRegion2D &source = {}, std::uint32_t rgba = 0xffffffffU,
      BlendMode blend = BlendMode::Alpha, ScissorRect scissor = {},
      ProgramHandle program = {}, std::uint32_t uniformSet = 0);
  [[nodiscard]] bool ninePatch(TextureHandle texture, const Rect2D &destination,
                               const TextureRegion2D &source,
                               const NinePatch2D &border,
                               std::uint32_t rgba = 0xffffffffU,
                               BlendMode blend = BlendMode::Alpha,
                               ScissorRect scissor = {},
                               ProgramHandle program = {},
                               std::uint32_t uniformSet = 0);
  [[nodiscard]] bool circle(float centerX, float centerY, float radius,
                            std::uint32_t rgba, int segments = 32,
                            BlendMode blend = BlendMode::Alpha,
                            ScissorRect scissor = {},
                            ProgramHandle program = {},
                            std::uint32_t uniformSet = 0);
  [[nodiscard]] bool line(float startX, float startY, float endX, float endY,
                          float width, std::uint32_t rgba,
                          BlendMode blend = BlendMode::Alpha,
                          ScissorRect scissor = {});
  [[nodiscard]] bool circleOutline(float centerX, float centerY, float radius,
                                   float width, std::uint32_t rgba,
                                   int segments = 32,
                                   BlendMode blend = BlendMode::Alpha,
                                   ScissorRect scissor = {});

  [[nodiscard]] bool flush(std::string &error);
  // Uniform values must remain valid until flush(). Registering an existing
  // non-zero id replaces that frame's values deterministically.
  void setUniformSet(std::uint32_t id,
                     std::span<const DrawUniformValue> uniforms);
  [[nodiscard]] const Canvas2DStatistics &statistics() const {
    return statistics_;
  }
  [[nodiscard]] TextureHandle whiteTexture() const { return whiteTexture_; }
  [[nodiscard]] ProgramHandle program() const { return program_; }

private:
  [[nodiscard]] bool add(TextureHandle texture, const Rect2D &destination,
                         const TextureRegion2D &source, std::uint32_t rgba,
                         BlendMode blend, ScissorRect scissor,
                         ProgramHandle program = {},
                         std::uint32_t uniformSet = 0);

  GpuResources &resources_;
  RenderCommands &commands_;
  QuadBatch batch_;
  TextureHandle whiteTexture_;
  SamplerHandle sampler_;
  ProgramHandle program_;
  std::unordered_map<std::uint32_t, std::span<const DrawUniformValue>>
      uniformSets_;
  std::uint16_t viewId_ = 0;
  Canvas2DStatistics statistics_;
};

} // namespace demi::runtime::render
