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
  assert(graphics.initialize(GraphicsDeviceConfig{.api = GraphicsApi::Noop,
                                                  .width = 64,
                                                  .height = 64,
                                                  .vsync = false},
                             error));

  auto resources = createBgfxGpuResources();
  auto commands = createBgfxRenderCommands(*resources);
  assert(commands);

  assert(!commands->configureView2D(
      View2DConfig{.width = 0, .height = 64}, error));
  assert(error.find("positive") != std::string::npos);
  assert(commands->configureView2D(
      View2DConfig{.id = 1,
                   .width = 64,
                   .height = 64,
                   .clearRgba = 0x102030ffU},
      error));

  const std::array<std::byte, 4> white = {
      std::byte{0xff}, std::byte{0xff}, std::byte{0xff}, std::byte{0xff}};
  const TextureHandle texture = resources->createTexture(
      TextureCreateInfo{.data = white, .debugName = "white"}, error);
  const SamplerHandle sampler = resources->createSampler("s_tex", error);
  const ProgramHandle program =
      resources->createBuiltinProgram(BuiltinProgram::Textured2D, error);
  assert(texture);
  assert(sampler);
  assert(program);

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
  const auto indexSpan =
      std::span<const std::uint16_t>(batch.indices())
          .subspan(range.firstIndex, range.indexCount);
  const TransientDraw draw{
      .viewId = 1,
      .vertices = std::as_bytes(vertexSpan),
      .vertexLayout = quadLayout(),
      .indices = indexSpan,
      .program = program,
      .texture = texture,
      .sampler = sampler,
  };
  assert(commands->submit(draw, error));
  static_cast<void>(graphics.endFrame());

  assert(resources->destroy(texture));
  assert(!commands->submit(draw, error));
  assert(error.find("stale texture") != std::string::npos);

  assert(resources->destroy(program));
  assert(resources->destroy(sampler));
  resources->clear();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
