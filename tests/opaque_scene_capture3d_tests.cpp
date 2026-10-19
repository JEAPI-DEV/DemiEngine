#include "demi/runtime/render/bgfx3d/OpaqueSceneCapture3D.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <set>

using namespace demi::runtime::render;

namespace {

class CaptureResources final : public GpuResources {
public:
  enum class Failure { None, FrameBuffer, DepthColor, DepthStorage, ColorCopy };
  Failure failure = Failure::None;
  RenderDeviceLimits device{
      .maxTextureSize = 1024, .maxViews = 64, .supportsTextureBlit = true};
  bool sampleableDepth = true;
  std::set<std::uint32_t> textures;
  std::set<std::uint32_t> frameBuffers;
  std::uint32_t next = 1;

  RenderDeviceLimits limits() const override { return device; }
  TextureCapabilities textureCapabilities(TextureFormat) const override {
    return {.sampled2D = sampleableDepth};
  }
  TextureHandle texture() {
    const auto index = next++;
    textures.insert(index);
    return {.index = index, .generation = 1};
  }
  TextureHandle createTexture(const TextureCreateInfo &info,
                              std::string &) override {
    assert(info.blitDestination && info.format == TextureFormat::RGBA8);
    return failure == Failure::ColorCopy ? TextureHandle{} : texture();
  }
  RenderTargetHandles createRenderTarget(const RenderTargetCreateInfo &info,
                                         std::string &) override {
    assert(info.colorFormat == TextureFormat::R32F && info.depth);
    assert(info.msaaSamples == 1 && info.filter == TextureFilter::Nearest);
    if (failure == Failure::FrameBuffer) {
      return {};
    }
    const auto frameBuffer = next++;
    frameBuffers.insert(frameBuffer);
    return {.frameBuffer = {.index = frameBuffer, .generation = 1},
            .color =
                failure == Failure::DepthColor ? TextureHandle{} : texture(),
            .depth =
                failure == Failure::DepthStorage ? TextureHandle{} : texture()};
  }
  bool destroy(TextureHandle handle) override {
    assert(textures.erase(handle.index) == 1);
    return true;
  }
  bool destroy(FrameBufferHandle handle) override {
    assert(frameBuffers.erase(handle.index) == 1);
    return true;
  }
  SamplerHandle createSampler(std::string_view, std::string &) override {
    return {};
  }
  UniformHandle createUniform(std::string_view, UniformType, std::uint16_t,
                              std::string &) override {
    return {};
  }
  BufferHandle createBuffer(const BufferCreateInfo &, std::string &) override {
    return {};
  }
  ProgramHandle createProgram(const ProgramCreateInfo &,
                              std::string &) override {
    return {};
  }
  ProgramHandle createBuiltinProgram(BuiltinProgram, std::string &) override {
    return {};
  }
  std::string_view shaderBackend() const override { return "test"; }
  bool destroy(SamplerHandle) override { return false; }
  bool destroy(UniformHandle) override { return false; }
  bool destroy(BufferHandle) override { return false; }
  bool destroy(ProgramHandle) override { return false; }
  void clear() override { assert(textures.empty() && frameBuffers.empty()); }
};

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
  failedAllocationNeverLeaksOrReplacesAnExistingCapture();
  camerasHaveIndependentCaptureAndPresentationLifetimes();
  unsupportedOrInvalidCapturesAllocateNothing();
  std::cout << "Opaque scene capture transaction and lifetime checks passed\n";
}
