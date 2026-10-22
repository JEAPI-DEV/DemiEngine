#include "demi/runtime/render/bgfx3d/OpaqueSceneCapture3D.h"
#include "demi/runtime/render/backend/QuadBatch.h"
#include "demi/runtime/render/backend/RenderCommands.h"
#include <array>
#include <span>

namespace demi::runtime::render {
WaterSceneCapture3D
OpaqueSceneCapture3D::Target::sample(bool originBottomLeft) const {
  return {.color = color,
          .linearDepth = depth.color,
          .width = width,
          .height = height,
          .originBottomLeft = originBottomLeft};
}

OpaqueSceneCapture3D::~OpaqueSceneCapture3D() { clear(); }

void OpaqueSceneCapture3D::destroy(Target &target) {
  if (target.opaqueFrameBuffer) {
    resources_.destroy(target.opaqueFrameBuffer);
  }
  if (target.scene.frameBuffer) {
    resources_.destroy(target.scene.frameBuffer);
  }
  if (target.scene.depth) {
    resources_.destroy(target.scene.depth);
  }
  if (target.scene.color) {
    resources_.destroy(target.scene.color);
  }
  if (target.analyticDepth && target.analyticDepth != target.depth.color) {
    resources_.destroy(target.analyticDepth);
  }
  if (target.depth.frameBuffer) {
    resources_.destroy(target.depth.frameBuffer);
  }
  if (target.depth.depth) {
    resources_.destroy(target.depth.depth);
  }
  if (target.depth.color) {
    resources_.destroy(target.depth.color);
  }
  if (target.color) {
    resources_.destroy(target.color);
  }
  target = {};
}

void OpaqueSceneCapture3D::clear() {
  for (auto &[view, target] : targets_) {
    destroy(target);
  }
  targets_.clear();
  if (resolveProgram_) {
    resources_.destroy(resolveProgram_);
  }
  if (resolveSampler_) {
    resources_.destroy(resolveSampler_);
  }
  if (resolveSamples_) {
    resources_.destroy(resolveSamples_);
  }
  resolveProgram_ = {};
  resolveSampler_ = {};
  resolveSamples_ = {};
}

void OpaqueSceneCapture3D::beginPresentation() {
  for (auto &[view, target] : targets_) {
    target.used = false;
  }
}

void OpaqueSceneCapture3D::endPresentation() {
  for (auto iterator = targets_.begin(); iterator != targets_.end();) {
    if (!iterator->second.used) {
      destroy(iterator->second);
      iterator = targets_.erase(iterator);
    } else {
      ++iterator;
    }
  }
}

bool OpaqueSceneCapture3D::allocatePrepass(Target &target, std::uint16_t width,
                                           std::uint16_t height,
                                           std::string &error) {
  target.depth =
      resources_.createRenderTarget({.width = width,
                                     .height = height,
                                     .colorFormat = TextureFormat::R32F,
                                     .depth = true,
                                     .debugName = "Water.opaqueDepth",
                                     .filter = TextureFilter::Nearest,
                                     .msaaSamples = 1},
                                    error);
  return target.depth.frameBuffer && target.depth.color && target.depth.depth;
}

bool OpaqueSceneCapture3D::allocateMrt(Target &target, std::uint16_t width,
                                       std::uint16_t height,
                                       std::string &error) {
  target.scene =
      resources_.createRenderTarget({.width = width,
                                     .height = height,
                                     .colorFormat = TextureFormat::RGBA8,
                                     .depth = true,
                                     .debugName = "Water.opaqueScene",
                                     .msaaSamples = target.requestedSamples},
                                    error);
  if (!target.scene.frameBuffer || !target.scene.color || !target.scene.depth) {
    return false;
  }
  if (target.scene.msaaSamples != target.requestedSamples) {
    error = "Opaque capture cannot lower the authored scene MSAA mode.";
    return false;
  }
  target.depth =
      resources_.createRenderTarget({.width = width,
                                     .height = height,
                                     .colorFormat = TextureFormat::R32F,
                                     .depth = false,
                                     .debugName = "Water.resolvedLinearDepth",
                                     .filter = TextureFilter::Nearest},
                                    error);
  if (!target.depth.frameBuffer || !target.depth.color) {
    return false;
  }
  if (target.requestedSamples == 1) {
    target.analyticDepth = target.depth.color;
  } else {
    target.analyticDepth =
        resources_.createTexture({.width = width,
                                  .height = height,
                                  .format = TextureFormat::R32F,
                                  .renderTarget = true,
                                  .filter = TextureFilter::Nearest,
                                  .debugName = "Water.multisampleLinearDepth",
                                  .msaaSamples = target.requestedSamples,
                                  .sampleMultisample = true},
                                 error);
  }
  if (!target.analyticDepth) {
    return false;
  }
  const std::array attachments{target.scene.color, target.analyticDepth,
                               target.scene.depth};
  target.opaqueFrameBuffer =
      resources_.createFrameBuffer(attachments, "Water.opaqueMrt", error);
  return bool(target.opaqueFrameBuffer);
}

OpaqueSceneCapture3D::Target *OpaqueSceneCapture3D::prepare(
    std::uint16_t cameraView, std::uint16_t width, std::uint16_t height,
    std::string &error, int msaaSamples, bool customDepthOutputUnsupported,
    bool preserveAuthoredTargetColor) {
  error.clear();
  const auto limits = resources_.limits();
  if (width == 0 || height == 0 || width > limits.maxTextureSize ||
      height > limits.maxTextureSize || cameraView >= limits.maxViews ||
      (msaaSamples != 1 && msaaSamples != 2 && msaaSamples != 4 &&
       msaaSamples != 8 && msaaSamples != 16)) {
    error = "Water capture dimensions, camera view or MSAA mode exceed device "
            "limits.";
    return nullptr;
  }
  const auto linear = resources_.textureCapabilities(TextureFormat::R32F);
  const auto color = resources_.textureCapabilities(TextureFormat::RGBA8);
  if (!limits.supportsTextureBlit || !linear.sampled2D) {
    error = "Water scene capture requires texture copies and sampleable R32F "
            "depth.";
    return nullptr;
  }
  PathReason reason = PathReason::AnalyticMrt;
  if (limits.maxFrameBufferAttachments < 3) {
    reason = PathReason::InsufficientAttachments;
  } else if (!linear.renderTarget || !color.renderTarget) {
    reason = PathReason::UnsupportedLinearDepthTarget;
  } else if (msaaSamples > 1 && (!linear.multisampleRenderTarget ||
                                 !color.multisampleRenderTarget)) {
    reason = PathReason::UnsupportedMultisampleTarget;
  } else if (msaaSamples > 1 && !linear.sampledMultisample) {
    reason = PathReason::UnsupportedMultisampleSampling;
  }
  if (reason == PathReason::AnalyticMrt && msaaSamples > 1 &&
      !limits.supportsMultisampleCountQuery) {
    reason = PathReason::UnsupportedMultisampleCountQuery;
  }
  if (reason == PathReason::AnalyticMrt && customDepthOutputUnsupported) {
    reason = PathReason::UnsupportedCustomDepthOutput;
  }
  // This is an output-ownership limitation, not a hardware capability. The
  // legacy path draws directly into an authored target and preserves its color.
  // An independently owned MRT scene cannot inherit that content implicitly.
  if (reason == PathReason::AnalyticMrt && preserveAuthoredTargetColor) {
    reason = PathReason::UnsupportedPreservedAuthoredColor;
  }
  auto &current = targets_[cameraView];
  current.used = true;
  if (current.width == width && current.height == height && current.color &&
      current.depth.frameBuffer && current.depth.color &&
      current.requestedSamples == msaaSamples && current.pathReason == reason) {
    return &current;
  }
  Target replacement;
  replacement.requestedSamples = msaaSamples;
  replacement.pathReason = reason;
  const bool allocated =
      replacement.usesMrt()
          ? allocateMrt(replacement, width, height, error)
          : allocatePrepass(replacement, width, height, error);
  if (allocated) {
    replacement.color =
        resources_.createTexture({.width = width,
                                  .height = height,
                                  .format = TextureFormat::RGBA8,
                                  .debugName = "Water.opaqueColor",
                                  .blitDestination = true},
                                 error);
  }
  if (!allocated || !replacement.color) {
    if (error.empty()) {
      error = "Water scene capture could not allocate complete color/depth "
              "targets.";
    }
    destroy(replacement);
    return nullptr;
  }
  replacement.width = width;
  replacement.height = height;
  replacement.used = true;
  destroy(current);
  current = replacement;
  return &current;
}

bool OpaqueSceneCapture3D::initializeResolve(std::string &error) {
  if (resolveProgram_ && resolveSampler_ && resolveSamples_) {
    return true;
  }
  const auto program = resources_.createBuiltinProgram(
      BuiltinProgram::LinearDepthResolve3D, error);
  const auto sampler =
      program ? resources_.createSampler("s_opaqueDepthMsaa", error)
              : SamplerHandle{};
  const auto samples =
      sampler ? resources_.createUniform("u_depthResolveSamples",
                                         UniformType::Vec4, 1, error)
              : UniformHandle{};
  if (!program || !sampler || !samples) {
    if (program) {
      resources_.destroy(program);
    }
    if (sampler) {
      resources_.destroy(sampler);
    }
    if (samples) {
      resources_.destroy(samples);
    }
    return false;
  }
  resolveProgram_ = program;
  resolveSampler_ = sampler;
  resolveSamples_ = samples;
  return true;
}

bool OpaqueSceneCapture3D::resolveDepth(Target &target,
                                        RenderCommands &commands,
                                        std::uint16_t view,
                                        std::string &error) {
  if (!target.usesMrt() || target.requestedSamples == 1) {
    return true;
  }
  if (!initializeResolve(error)) {
    return false;
  }
  if (!commands.configureView2D({.id = view,
                                 .width = target.width,
                                 .height = target.height,
                                 .clear = false,
                                 .frameBuffer = target.depth.frameBuffer,
                                 .debugName = "LinearDepthResolve"},
                                error)) {
    return false;
  }
  const float width = target.width;
  const float height = target.height;
  const std::array<QuadVertex, 4> vertices{
      {{.x = 0, .y = 0, .u = 0, .v = 0, .rgba = 0xffffffffU},
       {.x = width, .y = 0, .u = 1, .v = 0, .rgba = 0xffffffffU},
       {.x = width, .y = height, .u = 1, .v = 1, .rgba = 0xffffffffU},
       {.x = 0, .y = height, .u = 0, .v = 1, .rgba = 0xffffffffU}}};
  constexpr std::array<std::uint16_t, 6> indices{0, 1, 2, 0, 2, 3};
  const VertexLayout layout{
      .attributes = {{.semantic = VertexSemantic::Position,
                      .components = 2,
                      .type = VertexElementType::Float},
                     {.semantic = VertexSemantic::TexCoord0,
                      .components = 2,
                      .type = VertexElementType::Float},
                     {.semantic = VertexSemantic::Color0,
                      .components = 4,
                      .type = VertexElementType::UInt8,
                      .normalized = true}}};
  const std::array<float, 4> sampleCount{
      static_cast<float>(target.requestedSamples), 0, 0, 0};
  const std::array uniforms{
      DrawUniformValue{.handle = resolveSamples_, .values = sampleCount}};
  return commands.submit({.viewId = view,
                          .vertices = std::as_bytes(std::span(vertices)),
                          .vertexLayout = layout,
                          .indices = indices,
                          .program = resolveProgram_,
                          .texture = target.analyticDepth,
                          .sampler = resolveSampler_,
                          .blend = BlendMode::Opaque,
                          .state = {.blend = BlendMode::Opaque,
                                    .depthTest = DepthTest::Disabled,
                                    .writeDepth = false},
                          .uniforms = uniforms},
                         error);
}

} // namespace demi::runtime::render
