#include "demi/runtime/render/backend/BgfxDrawBindings.h"
#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/QuadBatch.h"
#include "demi/runtime/render/backend/RenderCommands.h"

#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace demi::runtime::render;

namespace {

class BindingLookup final : public BgfxResourceLookup {
public:
  mutable int uniformLookups = 0;
  mutable int textureLookups = 0;
  mutable int samplerLookups = 0;

  bgfx::UniformHandle bgfxUniform(UniformHandle handle) const override {
    ++uniformLookups;
    if (handle.generation != 1) {
      return BGFX_INVALID_HANDLE;
    }
    return {static_cast<std::uint16_t>(handle.index)};
  }
  bgfx::TextureHandle bgfxTexture(TextureHandle handle) const override {
    ++textureLookups;
    if (handle.generation != 1) {
      return BGFX_INVALID_HANDLE;
    }
    return {static_cast<std::uint16_t>(handle.index)};
  }
  bgfx::UniformHandle bgfxSampler(SamplerHandle handle) const override {
    ++samplerLookups;
    if (handle.generation != 1) {
      return BGFX_INVALID_HANDLE;
    }
    return {static_cast<std::uint16_t>(handle.index)};
  }
  bgfx::ProgramHandle bgfxProgram(ProgramHandle) const override {
    return BGFX_INVALID_HANDLE;
  }
  BgfxBufferReference bgfxBuffer(BufferHandle) const override { return {}; }
  bgfx::FrameBufferHandle bgfxFrameBuffer(FrameBufferHandle) const override {
    return BGFX_INVALID_HANDLE;
  }
};

class InstanceBatchCapture final : public RenderCommands {
public:
  std::uint32_t perDrawCapacity = 2;
  std::uint32_t remainingRecords = 20;
  std::size_t failAtSubmission = 0;
  std::vector<std::uint32_t> counts;
  std::vector<std::byte> records;

  std::uint32_t availableInstances(std::uint16_t,
                                   std::uint32_t requested) const override {
    return std::min({perDrawCapacity, remainingRecords, requested});
  }
  bool configureView2D(const View2DConfig &, std::string &) override {
    return true;
  }
  bool configureView3D(const View3DConfig &, std::string &) override {
    return true;
  }
  bool submit(const TransientDraw &, std::string &) override { return false; }
  bool submit(const BufferedDraw &, std::string &) override { return false; }
  bool submit(const InstancedBufferedDraw &draw, std::string &error) override {
    if (failAtSubmission == counts.size() + 1) {
      error = "injected instance submission failure";
      return false;
    }
    const auto count = static_cast<std::uint32_t>(draw.instances.data.size() /
                                                  draw.instances.stride);
    assert(count <= remainingRecords && count <= perDrawCapacity);
    counts.push_back(count);
    remainingRecords -= count;
    records.insert(records.end(), draw.instances.data.begin(),
                   draw.instances.data.end());
    return true;
  }
};

void testInstanceBatchSubmission() {
  std::array<std::array<float, 20>, 5> records{};
  for (std::size_t index = 0; index < records.size(); ++index) {
    records[index][0] = static_cast<float>(index);
  }
  const auto source = instanceData(std::span(records));
  InstancedBufferedDraw draw;
  draw.instances = source;
  std::string error;
  InstanceBatchCapture capture;
  const auto complete = capture.submitInstanceBatches(draw, error);
  assert(complete.complete && complete.submittedRecords == 5);
  assert(complete.submittedBatches == 3);
  assert((capture.counts == std::vector<std::uint32_t>{2, 2, 1}));
  assert(capture.records ==
         std::vector<std::byte>(source.data.begin(), source.data.end()));

  InstanceBatchCapture exhausted;
  exhausted.remainingRecords = 3;
  const auto partial = exhausted.submitInstanceBatches(draw, error);
  assert(!partial.complete && partial.submittedRecords == 3);
  assert(partial.submittedBatches == 2);
  assert(error.find("budget") != std::string::npos);
  assert(exhausted.records ==
         std::vector<std::byte>(source.data.begin(),
                                source.data.begin() + 3 * source.stride));

  InstanceBatchCapture failed;
  failed.failAtSubmission = 2;
  const auto rejected = failed.submitInstanceBatches(draw, error);
  assert(!rejected.complete && rejected.submittedRecords == 2);
  assert(rejected.submittedBatches == 1);
  assert(error == "injected instance submission failure");
  assert(failed.records.size() == 2 * source.stride);
  draw.instances.stride = 20;
  const auto invalid = failed.submitInstanceBatches(draw, error);
  assert(!invalid.complete && invalid.submittedRecords == 0);
}

void testDrawBindingResolution() {
  BindingLookup lookup;
  BgfxDrawBindings bindings;
  std::string error;
  std::array<float, 4> values{1, 2, 3, 4};
  std::array<DrawUniformValue, 2> uniforms{DrawUniformValue{{1, 1}, values, 1},
                                           DrawUniformValue{{2, 1}, values, 1}};
  assert(bindings.resolveUniforms(uniforms, lookup, error, "Test draw"));
  assert(lookup.uniformLookups == 2);
  assert(bindings.uniforms().size() == 2);
  assert(bindings.uniforms()[0].values.data() == values.data());

  // Resource generations are checked again even when the slot stays the same.
  uniforms.back().handle.generation = 2;
  assert(!bindings.resolveUniforms(uniforms, lookup, error, "Test draw"));
  assert(lookup.uniformLookups == 4 && bindings.uniforms().empty());
  uniforms.back().handle.generation = 1;
  values[0] = 5;
  assert(bindings.resolveUniforms(uniforms, lookup, error, "Test draw"));
  assert(bindings.uniforms()[0].values[0] == 5);

  std::array<DrawTextureBinding, 2> textures{
      DrawTextureBinding{1, {3, 1}, {4, 1}},
      DrawTextureBinding{2, {5, 1}, {6, 1}}};
  assert(bindings.resolveTextures(textures, lookup, 16, error));
  assert(lookup.textureLookups == 2 && lookup.samplerLookups == 2);
  assert(bindings.textures().size() == 2);
  textures.back().stage = 1;
  assert(!bindings.resolveTextures(textures, lookup, 16, error));
  assert(bindings.textures().empty());
  textures.back().stage = 16;
  assert(!bindings.resolveTextures(textures, lookup, 16, error));
  textures.back().stage = 0;
  assert(!bindings.resolveTextures(textures, lookup, 16, error));
  textures.back().stage = 2;
  textures.back().sampler.generation = 2;
  assert(!bindings.resolveTextures(textures, lookup, 16, error));
  textures.back().sampler.generation = 1;
  assert(bindings.resolveTextures(textures, lookup, 16, error));
  assert(bindings.resolveTextures({}, lookup, 16, error));
  assert(bindings.textures().empty());
}

VertexLayout quadLayout() {
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

int main() {
  testDrawBindingResolution();
  testInstanceBatchSubmission();
  BgfxGraphicsDevice graphics;
  std::string error;
  assert(graphics.initialize(
      GraphicsDeviceConfig{
          .api = GraphicsApi::Noop, .width = 64, .height = 64, .vsync = false},
      error));

  auto resources = createBgfxGpuResources();
  auto commands = createBgfxRenderCommands(*resources);
  assert(commands);

  assert(!commands->configureView2D(View2DConfig{.width = 0, .height = 64},
                                    error));
  assert(error.find("positive") != std::string::npos);
  assert(commands->configureView2D(
      View2DConfig{
          .id = 1, .width = 64, .height = 64, .clearRgba = 0x102030ffU},
      error));

  const std::array<std::byte, 4> white = {std::byte{0xff}, std::byte{0xff},
                                          std::byte{0xff}, std::byte{0xff}};
  const TextureHandle texture = resources->createTexture(
      TextureCreateInfo{.data = white, .debugName = "white"}, error);
  const SamplerHandle sampler = resources->createSampler("s_tex", error);
  const ProgramHandle program =
      resources->createBuiltinProgram(BuiltinProgram::Textured2D, error);
  const UniformHandle tint =
      resources->createUniform("u_testTint", UniformType::Vec4, 1, error);
  assert(texture);
  assert(sampler);
  assert(program);
  assert(tint);

  QuadBatch batch;
  assert(batch.add(QuadBatchKey{.texture = texture, .program = program},
                   Quad2D{.left = 4.0F,
                          .top = 8.0F,
                          .right = 20.0F,
                          .bottom = 24.0F,
                          .rgba = 0xff8040ffU}));
  assert(batch.draws().size() == 1);
  const auto &range = batch.draws().front();
  const auto vertexSpan =
      std::span<const QuadVertex>(batch.vertices()).subspan(range.firstVertex);
  const auto indexSpan = std::span<const std::uint16_t>(batch.indices())
                             .subspan(range.firstIndex, range.indexCount);
  const std::array<float, 4> tintValue{1.0F, 0.5F, 0.25F, 1.0F};
  const std::array<DrawUniformValue, 1> uniforms{{
      {.handle = tint, .values = tintValue},
  }};
  const TransientDraw draw{
      .viewId = 1,
      .vertices = std::as_bytes(vertexSpan),
      .vertexLayout = quadLayout(),
      .indices = indexSpan,
      .program = program,
      .texture = texture,
      .sampler = sampler,
      .uniforms = uniforms,
  };
  assert(commands->submit(draw, error));
  TransientDraw invalidUniformDraw = draw;
  const std::array<DrawUniformValue, 1> invalidUniforms{{
      {.handle = UniformHandle{}, .values = tintValue},
  }};
  invalidUniformDraw.uniforms = invalidUniforms;
  assert(!commands->submit(invalidUniformDraw, error));
  assert(error.find("invalid uniform") != std::string::npos);
  const BufferHandle vertexBuffer =
      resources->createBuffer({.kind = BufferKind::Vertex,
                               .data = std::as_bytes(vertexSpan),
                               .vertexLayout = quadLayout(),
                               .debugName = "test vertices"},
                              error);
  const BufferHandle indexBuffer =
      resources->createBuffer({.kind = BufferKind::Index16,
                               .data = std::as_bytes(indexSpan),
                               .debugName = "test indices"},
                              error);
  assert(vertexBuffer);
  assert(indexBuffer);
  const BufferedDraw buffered{
      .viewId = 1,
      .vertices = {.handle = vertexBuffer, .count = 4},
      .indices = {.handle = indexBuffer, .count = 6},
      .program = program,
      .texture = texture,
      .sampler = sampler,
      .state = {.blend = BlendMode::Alpha},
  };
  assert(commands->submit(buffered, error));
  const std::array<std::array<float, 16>, 2> transforms{{
      {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
       0.0F, 0.0F, 0.0F, 1.0F},
      {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
       2.0F, 0.0F, 0.0F, 1.0F},
  }};
  const InstancedBufferedDraw instanced{
      .viewId = 1,
      .vertices = {.handle = vertexBuffer, .count = 4},
      .indices = {.handle = indexBuffer, .count = 6},
      .program = program,
      .texture = texture,
      .sampler = sampler,
      .state = {.blend = BlendMode::Alpha},
      .instances =
          instanceData(std::span<const std::array<float, 16>>(transforms)),
  };
  assert(commands->submit(instanced, error));
  // Per-instance shader data occupies explicit records alongside its matrix.
  struct TerrainInstance {
    std::array<float, 16> transform;
    std::array<float, 4> patch;
  };
  static_assert(sizeof(TerrainInstance) == 80);
  const std::array<TerrainInstance, 2> terrainInstances{{
      {transforms[0], {0.0F, 0.0F, 1.0F, 0.0F}},
      {transforms[1], {8.0F, 0.0F, 1.0F, 0.0F}},
  }};
  auto generic = instanced;
  generic.instances = instanceData(std::span(terrainInstances));
  assert(generic.instances.stride == 80);
  assert(commands->availableInstances(generic.instances.stride, 2) == 2);
  assert(commands->submit(generic, error));
  const std::array<std::array<float, 24>, 2> wideInstances{};
  generic.instances = instanceData(std::span(wideInstances));
  assert(generic.instances.stride == 96);
  assert(commands->submit(generic, error));
  generic.instances.stride = 0;
  assert(!commands->submit(generic, error));
  generic.instances.stride = 20;
  assert(!commands->submit(generic, error));
  generic.instances = {.data =
                           std::as_bytes(std::span(terrainInstances)).first(79),
                       .stride = 80};
  assert(!commands->submit(generic, error));
  assert(commands->availableInstances(0, 1) == 0);
  assert(commands->availableInstances(20, 1) == 0);
  assert(commands->availableInstances(80, 0) == 0);
  // Availability is capped by actual transient bytes, not a 16-bit count.
  assert(commands->availableInstances(80, UINT32_MAX) < UINT32_MAX);

  const auto extraSampler = resources->createSampler("s_extra", error);
  std::array<DrawTextureBinding, 1> extra{
      {{.stage = 1, .texture = texture, .sampler = extraSampler}}};
  auto textured = buffered;
  textured.textures = extra;
  auto texturedInstances = instanced;
  texturedInstances.textures = extra;
  assert(commands->submit(textured, error) &&
         commands->submit(texturedInstances, error));
  extra[0].stage = 0;
  assert(!commands->submit(textured, error));
  extra[0].stage = 1;
  const std::array<DrawTextureBinding, 2> duplicate{extra[0], extra[0]};
  textured.textures = duplicate;
  assert(!commands->submit(textured, error));
  textured.textures = extra;
  assert(resources->destroy(extraSampler));
  assert(!commands->submit(textured, error) &&
         !commands->submit(texturedInstances, error));
  InstancedBufferedDraw noInstances = instanced;
  noInstances.instances = {};
  assert(!commands->submit(noInstances, error));
  assert(error.find("at least one instance record") != std::string::npos);
  BufferedDraw mismatched = buffered;
  mismatched.indices.handle = vertexBuffer;
  assert(!commands->submit(mismatched, error));
  assert(error.find("non-index") != std::string::npos);
  BufferedDraw empty = buffered;
  empty.vertices.count = 0;
  assert(!commands->submit(empty, error));
  assert(error.find("must not be empty") != std::string::npos);
  static_cast<void>(graphics.endFrame());

  assert(resources->destroy(texture));
  assert(!commands->submit(draw, error));
  assert(error.find("stale texture") != std::string::npos);
  assert(resources->destroy(vertexBuffer));
  assert(!commands->submit(buffered, error));
  assert(error.find("stale or non-vertex") != std::string::npos);

  assert(resources->destroy(program));
  assert(resources->destroy(sampler));
  assert(resources->destroy(tint));
  assert(resources->destroy(indexBuffer));
  resources->clear();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
