#include "demi/runtime/render/bgfx3d/PostProcessRenderer3D.h"

#include "demi/runtime/render/backend/ImageColorSpace.h"
#include "demi/runtime/render/backend/QuadBatch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <span>
#include <vector>

namespace demi::runtime::render {
namespace {

constexpr std::array underwaterUniformNames{
    "u_underwaterOptics",  "u_underwaterColor",  "u_underwaterPlane",
    "u_underwaterForward", "u_underwaterRight",  "u_underwaterUp",
    "u_underwaterBasisX",  "u_underwaterBasisY", "u_underwaterBasisZ"};

Vec3 normalized(Vec3 value, Vec3 fallback) {
  const float length = std::hypot(value.x, value.y, value.z);
  if (!(length > 0)) {
    return fallback;
  }
  return {value.x / length, value.y / length, value.z / length};
}

Vec3 cross(Vec3 first, Vec3 second) {
  return {first.y * second.z - first.z * second.y,
          first.z * second.x - first.x * second.z,
          first.x * second.y - first.y * second.x};
}

VertexLayout quadVertexLayout() {
  return VertexLayout{.attributes = {
                          {.semantic = VertexSemantic::Position,
                           .components = 2,
                           .type = VertexElementType::Float},
                          {.semantic = VertexSemantic::TexCoord0,
                           .components = 2,
                           .type = VertexElementType::Float},
                          {.semantic = VertexSemantic::Color0,
                           .components = 4,
                           .type = VertexElementType::UInt8,
                           .normalized = true},
                      }};
}

} // namespace

PostProcessRenderer3D::PostProcessRenderer3D(GpuResources &resources,
                                             RenderCommands &commands)
    : resources_(resources), commands_(commands) {}

PostProcessRenderer3D::~PostProcessRenderer3D() { shutdown(); }

bool PostProcessRenderer3D::initialize(std::string &error) {
  if (program_)
    return true;
  program_ =
      resources_.createBuiltinProgram(BuiltinProgram::PostProcess2D, error);
  sampler_ = resources_.createSampler("s_postColor", error);
  colorAdjustUniform_ =
      resources_.createUniform("u_colorAdjust", UniformType::Vec4, 1, error);
  tintUniform_ =
      resources_.createUniform("u_tint", UniformType::Vec4, 1, error);
  bloomFadeUniform_ =
      resources_.createUniform("u_bloomFade", UniformType::Vec4, 1, error);
  fadeColorUniform_ =
      resources_.createUniform("u_fadeColor", UniformType::Vec4, 1, error);
  underwaterDepthSampler_ =
      resources_.createSampler("s_underwaterDepth", error);
  for (std::size_t index = 0; index < underwaterUniformNames.size(); ++index) {
    underwaterUniforms_[index] = resources_.createUniform(
        underwaterUniformNames[index], UniformType::Vec4, 1, error);
  }
  if (!program_ || !sampler_ || !colorAdjustUniform_ || !tintUniform_ ||
      !bloomFadeUniform_ || !fadeColorUniform_ || !underwaterDepthSampler_ ||
      !std::ranges::all_of(underwaterUniforms_,
                           [](auto handle) { return bool(handle); })) {
    shutdown();
    return false;
  }
  return true;
}

void PostProcessRenderer3D::destroyScratchTarget() {
  if (scratch_.frameBuffer)
    resources_.destroy(scratch_.frameBuffer);
  if (scratch_.depth)
    resources_.destroy(scratch_.depth);
  if (scratch_.color)
    resources_.destroy(scratch_.color);
  scratch_ = {};
  scratchWidth_ = 0;
  scratchHeight_ = 0;
  scratchSamples_ = 1;
}

void PostProcessRenderer3D::shutdown() {
  destroyScratchTarget();
  for (const UniformHandle uniform : {colorAdjustUniform_, tintUniform_,
                                      bloomFadeUniform_, fadeColorUniform_})
    if (uniform)
      resources_.destroy(uniform);
  if (sampler_)
    resources_.destroy(sampler_);
  if (program_)
    resources_.destroy(program_);
  if (underwaterDepthSampler_) {
    resources_.destroy(underwaterDepthSampler_);
    underwaterDepthSampler_ = {};
  }
  for (auto &uniform : underwaterUniforms_) {
    if (uniform) {
      resources_.destroy(uniform);
      uniform = {};
    }
  }
  colorAdjustUniform_ = {};
  tintUniform_ = {};
  bloomFadeUniform_ = {};
  fadeColorUniform_ = {};
  sampler_ = {};
  program_ = {};
}

void PostProcessRenderer3D::setMsaaSamples(int samples) {
  // Turning AA off must retire its attachments even if no post pass follows.
  if (scratch_.frameBuffer && scratchSamples_ != samples)
    destroyScratchTarget();
}

const RenderTargetHandles &
PostProcessRenderer3D::scratchTarget(const std::uint16_t width,
                                     const std::uint16_t height,
                                     std::string &error, int msaaSamples) {
  if (width == 0 || height == 0) {
    error = "Post-process target dimensions must be positive.";
    destroyScratchTarget();
    return scratch_;
  }
  if (scratch_.frameBuffer && scratchWidth_ == width &&
      scratchHeight_ == height && scratchSamples_ == msaaSamples)
    return scratch_;
  const auto replacement =
      resources_.createRenderTarget({.width = width,
                                     .height = height,
                                     .colorFormat = TextureFormat::RGBA8,
                                     .depth = true,
                                     .debugName = "3D scene resolve",
                                     .msaaSamples = msaaSamples},
                                    error);
  if (replacement.frameBuffer && replacement.color) {
    destroyScratchTarget();
    scratch_ = replacement;
    scratchWidth_ = width;
    scratchHeight_ = height;
    scratchSamples_ = msaaSamples;
  } else {
    // A failed replacement must not be mistaken for a valid new sample mode.
    destroyScratchTarget();
  }
  return scratch_;
}

bool PostProcessRenderer3D::present(
    const BgfxCameraFrame3D &frame, const TextureHandle source,
    const FrameBufferHandle destination, std::string &error,
    std::optional<std::uint16_t> explicitViewId) {
  if (!program_ || !sampler_ || !source) {
    error = "Post-process rendering requires initialized GPU resources.";
    return false;
  }
  const auto presentationView =
      explicitViewId.value_or(static_cast<std::uint16_t>(frame.viewId + 2U));
  if (!commands_.configureView2D(
          {.id = presentationView,
           .x = destination ? std::uint16_t{0} : frame.viewportX,
           .y = destination ? std::uint16_t{0} : frame.viewportY,
           .width = std::max<std::uint16_t>(frame.viewportWidth, 1),
           .height = std::max<std::uint16_t>(frame.viewportHeight, 1),
           .clear = false,
           .frameBuffer = destination,
           .debugName = "PostProcess"},
          error))
    return false;

  const float width = static_cast<float>(frame.viewportWidth);
  const float height = static_cast<float>(frame.viewportHeight);
  constexpr std::uint32_t White = 0xffffffffU;
  const std::array<QuadVertex, 4> vertices{{
      {.x = 0.0F, .y = 0.0F, .u = 0.0F, .v = 0.0F, .rgba = White},
      {.x = width, .y = 0.0F, .u = 1.0F, .v = 0.0F, .rgba = White},
      {.x = width, .y = height, .u = 1.0F, .v = 1.0F, .rgba = White},
      {.x = 0.0F, .y = height, .u = 0.0F, .v = 1.0F, .rgba = White},
  }};
  constexpr std::array<std::uint16_t, 6> Indices{0, 1, 2, 0, 2, 3};
  const PostProcessStackComponent effect =
      frame.postProcess.value_or(PostProcessStackComponent{});
  const std::array<float, 4> colorAdjust{effect.exposure, effect.contrast,
                                         effect.saturation, effect.vignette};
  const std::array<float, 4> tint{effect.tint.r, effect.tint.g, effect.tint.b,
                                  effect.tint.a};
  const std::array<float, 4> bloomFade{effect.bloom, effect.bloomThreshold,
                                       effect.fade, 0.0F};
  const std::array<float, 4> fadeColor{effect.fadeColor.r, effect.fadeColor.g,
                                       effect.fadeColor.b, effect.fadeColor.a};
  std::vector<DrawUniformValue> uniforms{
      {.handle = colorAdjustUniform_, .values = colorAdjust},
      {.handle = tintUniform_, .values = tint},
      {.handle = bloomFadeUniform_, .values = bloomFade},
      {.handle = fadeColorUniform_, .values = fadeColor},
  };
  std::array<std::array<float, 4>, underwaterUniformNames.size()>
      underwaterValues{};
  if (frame.underwater) {
    const auto &water = *frame.underwater;
    if (!water.linearDepth) {
      error =
          "Underwater presentation requires an opaque scene depth snapshot.";
      return false;
    }
    const auto forward = normalized(frame.forward, {0, 0, -1});
    const auto right = normalized(cross(forward, frame.up), {1, 0, 0});
    const auto up = normalized(cross(right, forward), {0, 1, 0});
    const float extent =
        frame.camera.perspective
            ? std::tan(frame.camera.fov * std::numbers::pi_v<float> / 360.F)
            : frame.camera.orthographicSize;
    const float aspect = float(frame.viewportWidth) /
                         std::max(frame.viewportHeight, std::uint16_t{1});
    const float planeDistance = water.normal.x * water.surfaceRelative.x +
                                water.normal.y * water.surfaceRelative.y +
                                water.normal.z * water.surfaceRelative.z;
    underwaterValues = {
        {{1, 1.F / water.absorptionDistance, water.immersion,
          frame.camera.perspective ? 1.F : 0.F},
         {float(decodeSrgbChannel(water.color.r)),
          float(decodeSrgbChannel(water.color.g)),
          float(decodeSrgbChannel(water.color.b)), water.color.a},
         {water.normal.x, water.normal.y, water.normal.z, planeDistance},
         {forward.x, forward.y, forward.z, frame.camera.farClip},
         {right.x, right.y, right.z, extent * aspect},
         {up.x, up.y, up.z, extent},
         {water.inverseBasis[0].x, water.inverseBasis[0].y,
          water.inverseBasis[0].z, 0},
         {water.inverseBasis[1].x, water.inverseBasis[1].y,
          water.inverseBasis[1].z, 0},
         {water.inverseBasis[2].x, water.inverseBasis[2].y,
          water.inverseBasis[2].z, 0}}};
  }
  for (std::size_t index = 0; index < underwaterValues.size(); ++index) {
    uniforms.push_back({.handle = underwaterUniforms_[index],
                        .values = underwaterValues[index]});
  }
  const std::array textures{DrawTextureBinding{
      .stage = 1,
      .texture = frame.underwater ? frame.underwater->linearDepth : source,
      .sampler = underwaterDepthSampler_}};
  return commands_.submit({.viewId = presentationView,
                           .vertices = std::as_bytes(std::span(vertices)),
                           .vertexLayout = quadVertexLayout(),
                           .indices = Indices,
                           .program = program_,
                           .texture = source,
                           .sampler = sampler_,
                           .blend = BlendMode::Opaque,
                           .state = {.blend = BlendMode::Opaque,
                                     .depthTest = DepthTest::Disabled,
                                     .writeDepth = false},
                           .scissor = {},
                           .uniforms = uniforms,
                           .textures = textures},
                          error);
}

bool hasPostProcessEffects(
    const std::optional<PostProcessStackComponent> &postProcess) {
  if (!postProcess)
    return false;
  const PostProcessStackComponent &effect = *postProcess;
  return std::abs(effect.exposure) > 0.0001F ||
         std::abs(effect.contrast - 1.0F) > 0.0001F ||
         std::abs(effect.saturation - 1.0F) > 0.0001F ||
         std::abs(effect.tint.r - 1.0F) > 0.0001F ||
         std::abs(effect.tint.g - 1.0F) > 0.0001F ||
         std::abs(effect.tint.b - 1.0F) > 0.0001F ||
         effect.vignette > 0.0001F || effect.bloom > 0.0001F ||
         effect.fade > 0.0001F;
}

} // namespace demi::runtime::render
