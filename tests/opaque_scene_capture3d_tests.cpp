#include "demi/runtime/render/backend/RenderCommands.h"
#include "demi/runtime/render/bgfx3d/OpaqueSceneCapture3D.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <iostream>
#include <map>
#include <set>
#include <vector>

using namespace demi::runtime::render;

namespace {

class CaptureResources final : public GpuResources {
public:
  enum class Failure {
    None,
    FrameBuffer,
    DepthColor,
    DepthStorage,
    ColorCopy,
    MrtFrameBuffer,
    MultisampleTexture
  };
  Failure failure = Failure::None;
  RenderDeviceLimits device{
      .maxTextureSize = 1024, .maxViews = 64, .supportsTextureBlit = true};
  bool sampleableDepth = true;
  bool supportsMrt = false;
  bool supportsMsaaSampling = true;
  bool lowerSceneSamples = false;
  std::map<std::uint32_t, TextureCreateInfo> textureDescriptions;
  std::map<std::uint32_t, std::vector<TextureHandle>> attachments;
  std::set<std::uint32_t> programs;
  std::set<std::uint32_t> samplers;
  std::set<std::uint32_t> uniforms;
  std::set<std::uint32_t> textures;
  std::set<std::uint32_t> frameBuffers;
  std::uint32_t next = 1;

  RenderDeviceLimits limits() const override { return device; }
  TextureCapabilities textureCapabilities(TextureFormat) const override {
    return {.sampled2D = sampleableDepth,
            .renderTarget = supportsMrt,
            .multisampleRenderTarget = supportsMrt,
            .sampledMultisample = supportsMsaaSampling};
  }
  TextureHandle texture() {
    const auto index = next++;
    textures.insert(index);
    return {.index = index, .generation = 1};
  }
  TextureHandle createTexture(const TextureCreateInfo &info,
                              std::string &) override {
    if (info.sampleMultisample) {
      assert(info.renderTarget && info.msaaSamples > 1);
      assert(info.format == TextureFormat::R32F);
      if (failure == Failure::MultisampleTexture) {
        return {};
      }
    } else {
      assert(info.blitDestination && info.format == TextureFormat::RGBA8);
      if (failure == Failure::ColorCopy) {
        return {};
      }
    }
    const auto result = texture();
    textureDescriptions.emplace(result.index, info);
    return result;
  }
  RenderTargetHandles createRenderTarget(const RenderTargetCreateInfo &info,
                                         std::string &) override {
    if (!supportsMrt) {
      assert(info.colorFormat == TextureFormat::R32F && info.depth);
      assert(info.msaaSamples == 1 && info.filter == TextureFilter::Nearest);
    }
    if (failure == Failure::FrameBuffer) {
      return {};
    }
    const auto frameBuffer = next++;
    frameBuffers.insert(frameBuffer);
    const auto color =
        failure == Failure::DepthColor ? TextureHandle{} : texture();
    const auto depth = info.depth && failure != Failure::DepthStorage
                           ? texture()
                           : TextureHandle{};
    if (color) {
      attachments[frameBuffer].push_back(color);
    }
    if (depth) {
      attachments[frameBuffer].push_back(depth);
    }
    return {.frameBuffer = {.index = frameBuffer, .generation = 1},
            .color = color,
            .depth = depth,
            .msaaSamples =
                lowerSceneSamples && info.colorFormat == TextureFormat::RGBA8
                    ? 1
                    : info.msaaSamples};
  }
  FrameBufferHandle createFrameBuffer(std::span<const TextureHandle> textures,
                                      const std::string &,
                                      std::string &) override {
    assert(supportsMrt && textures.size() == 3);
    if (failure == Failure::MrtFrameBuffer) {
      return {};
    }
    const auto index = next++;
    frameBuffers.insert(index);
    attachments[index] = {textures.begin(), textures.end()};
    return {.index = index, .generation = 1};
  }
  bool destroy(TextureHandle handle) override {
    for (const auto &[frameBuffer, bound] : attachments) {
      (void)frameBuffer;
      assert(std::find(bound.begin(), bound.end(), handle) == bound.end());
    }
    assert(textures.erase(handle.index) == 1);
    textureDescriptions.erase(handle.index);
    return true;
  }
  bool destroy(FrameBufferHandle handle) override {
    assert(frameBuffers.erase(handle.index) == 1);
    attachments.erase(handle.index);
    return true;
  }
  SamplerHandle createSampler(std::string_view name, std::string &) override {
    assert(name == "s_opaqueDepthMsaa");
    const auto index = next++;
    samplers.insert(index);
    return {.index = index, .generation = 1};
  }
  UniformHandle createUniform(std::string_view name, UniformType type,
                              std::uint16_t count, std::string &) override {
    assert(name == "u_depthResolveSamples" && type == UniformType::Vec4 &&
           count == 1);
    const auto index = next++;
    uniforms.insert(index);
    return {.index = index, .generation = 1};
  }
  BufferHandle createBuffer(const BufferCreateInfo &, std::string &) override {
    return {};
  }
  ProgramHandle createProgram(const ProgramCreateInfo &,
                              std::string &) override {
    return {};
  }
  ProgramHandle createBuiltinProgram(BuiltinProgram program,
                                     std::string &) override {
    assert(program == BuiltinProgram::LinearDepthResolve3D);
    const auto index = next++;
    programs.insert(index);
    return {.index = index, .generation = 1};
  }
  std::string_view shaderBackend() const override { return "test"; }
  bool destroy(SamplerHandle handle) override {
    return samplers.erase(handle.index) == 1;
  }
  bool destroy(UniformHandle handle) override {
    return uniforms.erase(handle.index) == 1;
  }
  bool destroy(BufferHandle) override { return false; }
  bool destroy(ProgramHandle handle) override {
    return programs.erase(handle.index) == 1;
  }
  void clear() override { assert(textures.empty() && frameBuffers.empty()); }
};

class CaptureCommands final : public RenderCommands {
public:
  FrameBufferHandle destination;
  TextureHandle source;
  std::uint16_t view = 0;
  float samples = 0;
  int draws = 0;
  bool configureView2D(const View2DConfig &config, std::string &) override {
    assert(!config.clear && config.debugName == "LinearDepthResolve");
    destination = config.frameBuffer;
    view = config.id;
    return true;
  }
  bool configureView3D(const View3DConfig &, std::string &) override {
    return false;
  }
  bool submit(const TransientDraw &draw, std::string &) override {
    assert(draw.viewId == view && draw.indices.size() == 6);
    assert(draw.state.depthTest == DepthTest::Disabled &&
           !draw.state.writeDepth);
    assert(draw.uniforms.size() == 1);
    samples = draw.uniforms[0].values[0];
    source = draw.texture;
    ++draws;
    return true;
  }
  bool submit(const BufferedDraw &, std::string &) override { return false; }
  bool submit(const InstancedBufferedDraw &, std::string &) override {
    return false;
  }
};

void analyticMrtSharesSceneAttachmentsAndResolvesWithoutFeedback() {
  CaptureResources resources;
  resources.supportsMrt = true;
  resources.device.maxFrameBufferAttachments = 3;
  resources.device.supportsMultisampleCountQuery = true;
  OpaqueSceneCapture3D captures(resources);
  CaptureCommands commands;
  std::string error;
  auto *target = captures.prepare(4, 100, 80, error, 4);
  assert(target && target->usesMrt() && error.empty());
  assert(target->scene.msaaSamples == 4 && target->requestedSamples == 4);
  assert(target->scene.frameBuffer != target->opaqueFrameBuffer);
  const auto &bound = resources.attachments.at(target->opaqueFrameBuffer.index);
  assert(bound[0] == target->scene.color);
  assert(bound[1] == target->analyticDepth);
  assert(bound[2] == target->scene.depth);
  assert(target->analyticDepth != target->depth.color);
  assert(!target->depth.depth);
  const auto &multisample =
      resources.textureDescriptions.at(target->analyticDepth.index);
  assert(multisample.sampleMultisample && multisample.msaaSamples == 4);
  assert(captures.resolveDepth(*target, commands, 10, error));
  assert(commands.draws == 1 && commands.samples == 4);
  assert(commands.source == target->analyticDepth);
  assert(commands.destination == target->depth.frameBuffer);
  assert(target->sample(false).linearDepth == target->depth.color);
  assert(resources.textures.size() == 5 && resources.frameBuffers.size() == 3);

  const auto retainedColor = target->scene.color;
  for (const auto failure : {CaptureResources::Failure::MrtFrameBuffer,
                             CaptureResources::Failure::MultisampleTexture,
                             CaptureResources::Failure::ColorCopy}) {
    resources.failure = failure;
    assert(!captures.prepare(4, 200, 160, error, 4));
    assert(resources.textures.size() == 5 &&
           resources.frameBuffers.size() == 3);
    target = captures.prepare(4, 100, 80, error, 4);
    assert(target && target->scene.color == retainedColor);
  }
  resources.failure = CaptureResources::Failure::None;
  auto *second = captures.prepare(16, 200, 160, error, 1);
  assert(second && second->usesMrt());
  assert(second->analyticDepth == second->depth.color);
  assert(second->scene.color != target->scene.color);
  assert(captures.resolveDepth(*second, commands, 22, error));
  assert(commands.draws == 1);
  captures.clear();
  assert(resources.textures.empty() && resources.frameBuffers.empty());
  assert(resources.programs.empty() && resources.samplers.empty() &&
         resources.uniforms.empty());
}

void preservedAuthoredTargetColorKeepsLegacyDestinationOwnership() {
  CaptureResources resources;
  resources.supportsMrt = true;
  resources.device.maxFrameBufferAttachments = 3;
  resources.device.supportsMultisampleCountQuery = true;
  OpaqueSceneCapture3D captures(resources);
  demi::runtime::Camera3DComponent camera;
  camera.renderTarget = "asset://camera/persistent";
  std::string error;
  for (const auto clearMode : {"depth", "none"}) {
    camera.clearMode = clearMode;
    const auto preserve =
        OpaqueSceneCapture3D::requiresPreservedAuthoredColor(camera);
    assert(preserve);
    auto *target = captures.prepare(4, 100, 80, error, 4, false, preserve);
    assert(target && error.empty());
    assert(target->pathReason ==
           OpaqueSceneCapture3D::PathReason::UnsupportedPreservedAuthoredColor);
    assert(!target->usesMrt());
    // No replacement scene color/destination exists: renderView keeps drawing
    // directly to the authored target, with its original color/depth clear
    // mode.
    assert(!target->scene.frameBuffer && !target->scene.color &&
           !target->opaqueFrameBuffer);
    assert(target->depth.frameBuffer && target->depth.color);
  }
  camera.clearMode = "color";
  assert(!OpaqueSceneCapture3D::requiresPreservedAuthoredColor(camera));
  auto *target = captures.prepare(
      4, 100, 80, error, 4, false,
      OpaqueSceneCapture3D::requiresPreservedAuthoredColor(camera));
  assert(target && target->usesMrt() && target->scene.color);
  camera.renderTarget.clear();
  camera.clearMode = "none";
  assert(!OpaqueSceneCapture3D::requiresPreservedAuthoredColor(camera));
}

void explicitCapabilityReasonsPreserveMsaaAndUsePrepassOnlyWhenRequired() {
  CaptureResources resources;
  resources.supportsMrt = true;
  resources.device.maxFrameBufferAttachments = 3;
  resources.device.supportsMultisampleCountQuery = true;
  OpaqueSceneCapture3D captures(resources);
  std::string error;
  resources.supportsMsaaSampling = false;
  auto *target = captures.prepare(4, 100, 80, error, 4);
  assert(target && !target->usesMrt());
  assert(target->pathReason ==
         OpaqueSceneCapture3D::PathReason::UnsupportedMultisampleSampling);
  assert(target->requestedSamples == 4);
  resources.supportsMsaaSampling = true;
  resources.device.supportsMultisampleCountQuery = false;
  target = captures.prepare(4, 100, 80, error, 4);
  assert(
      target &&
      target->pathReason ==
          OpaqueSceneCapture3D::PathReason::UnsupportedMultisampleCountQuery);
  resources.device.supportsMultisampleCountQuery = true;
  target = captures.prepare(4, 100, 80, error, 4, true);
  assert(target &&
         target->pathReason ==
             OpaqueSceneCapture3D::PathReason::UnsupportedCustomDepthOutput);
  assert(!target->usesMrt());
  captures.clear();
  resources.lowerSceneSamples = true;
  assert(!captures.prepare(4, 100, 80, error, 4));
  assert(error.find("MSAA") != std::string::npos);
  assert(resources.textures.empty() && resources.frameBuffers.empty());
}

void failedAllocationNeverLeaksOrReplacesAnExistingCapture() {
  CaptureResources resources;
  OpaqueSceneCapture3D captures(resources);
  std::string error;
  for (const auto failure : {CaptureResources::Failure::FrameBuffer,
                             CaptureResources::Failure::DepthColor,
                             CaptureResources::Failure::DepthStorage,
                             CaptureResources::Failure::ColorCopy}) {
    resources.failure = failure;
    assert(!captures.prepare(4, 100, 80, error));
    assert(!error.empty());
    assert(resources.textures.empty() && resources.frameBuffers.empty());
  }
  resources.failure = CaptureResources::Failure::None;
  const auto *first = captures.prepare(4, 100, 80, error);
  assert(first && error.empty());
  const auto original = first->sample(false);
  assert(resources.textures.size() == 3 && resources.frameBuffers.size() == 1);
  for (const auto failure : {CaptureResources::Failure::FrameBuffer,
                             CaptureResources::Failure::DepthColor,
                             CaptureResources::Failure::DepthStorage,
                             CaptureResources::Failure::ColorCopy}) {
    resources.failure = failure;
    assert(!captures.prepare(4, 200, 160, error));
    assert(resources.textures.size() == 3 &&
           resources.frameBuffers.size() == 1);
    const auto *retained = captures.prepare(4, 100, 80, error);
    assert(retained && error.empty());
    assert(retained->sample(false).color == original.color);
    assert(retained->sample(false).linearDepth == original.linearDepth);
  }
  resources.failure = CaptureResources::Failure::None;
  const auto *replacement = captures.prepare(4, 200, 160, error);
  assert(replacement && replacement->sample(false).color != original.color);
  assert(replacement->sample(false).width == 200 &&
         replacement->sample(false).height == 160);
  assert(resources.textures.size() == 3 && resources.frameBuffers.size() == 1);
  captures.clear();
  assert(resources.textures.empty() && resources.frameBuffers.empty());
}

void camerasHaveIndependentCaptureAndPresentationLifetimes() {
  CaptureResources resources;
  OpaqueSceneCapture3D captures(resources);
  std::string error;
  captures.beginPresentation();
  const auto first = captures.prepare(4, 100, 80, error)->sample(false);
  const auto second = captures.prepare(16, 100, 80, error)->sample(true);
  assert(first.color != second.color &&
         first.linearDepth != second.linearDepth);
  assert(!first.originBottomLeft && second.originBottomLeft);
  captures.endPresentation();
  assert(resources.textures.size() == 6 && resources.frameBuffers.size() == 2);
  captures.beginPresentation();
  assert(captures.prepare(16, 100, 80, error)->sample(true).color ==
         second.color);
  captures.endPresentation();
  assert(resources.textures.size() == 3 &&
         !resources.textures.contains(first.color.index));
  captures.beginPresentation();
  captures.endPresentation();
  assert(resources.textures.empty() && resources.frameBuffers.empty());
}

void unsupportedOrInvalidCapturesAllocateNothing() {
  CaptureResources resources;
  OpaqueSceneCapture3D captures(resources);
  std::string error;
  assert(!captures.prepare(0, 0, 80, error));
  assert(!captures.prepare(0, 100, 0, error));
  assert(!captures.prepare(0, 1025, 80, error));
  assert(!captures.prepare(64, 100, 80, error));
  resources.device.supportsTextureBlit = false;
  assert(!captures.prepare(0, 100, 80, error));
  resources.device.supportsTextureBlit = true;
  resources.sampleableDepth = false;
  assert(!captures.prepare(0, 100, 80, error));
  assert(resources.textures.empty() && resources.frameBuffers.empty());
}

} // namespace

int main() {
  analyticMrtSharesSceneAttachmentsAndResolvesWithoutFeedback();
  preservedAuthoredTargetColorKeepsLegacyDestinationOwnership();
  explicitCapabilityReasonsPreserveMsaaAndUsePrepassOnlyWhenRequired();
  failedAllocationNeverLeaksOrReplacesAnExistingCapture();
  camerasHaveIndependentCaptureAndPresentationLifetimes();
  unsupportedOrInvalidCapturesAllocateNothing();
  std::cout << "Opaque scene capture transaction and lifetime checks passed\n";
}
