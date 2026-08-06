#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/QuadBatch.h"
#include "demi/runtime/render/backend/RenderCommands.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

using namespace demi::runtime::render;

namespace {

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
      .transforms = transforms,
  };
  assert(commands->submit(instanced, error));
  InstancedBufferedDraw noInstances = instanced;
  noInstances.transforms = {};
  assert(!commands->submit(noInstances, error));
  assert(error.find("at least one transform") != std::string::npos);
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
