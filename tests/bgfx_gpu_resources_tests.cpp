#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/GpuResources.h"

#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

using namespace demi::runtime::render;

int main() {
  BgfxGraphicsDevice graphics;
  std::string error;
  assert(graphics.initialize(
      GraphicsDeviceConfig{
          .api = GraphicsApi::Noop, .width = 64, .height = 64, .vsync = false},
      error));
  auto resources = createBgfxGpuResources();

  const std::array<std::byte, 16> pixels{};
  const TextureHandle texture =
      resources->createTexture(TextureCreateInfo{.width = 2,
                                                 .height = 2,
                                                 .format = TextureFormat::RGBA8,
                                                 .data = pixels,
                                                 .debugName = "test texture"},
                               error);
  assert(texture);
  const SamplerHandle sampler = resources->createSampler("s_test", error);
  assert(sampler);

  const std::array<std::byte, 6> indices{};
  const BufferHandle indexBuffer =
      resources->createBuffer(BufferCreateInfo{.kind = BufferKind::Index16,
                                               .data = indices,
                                               .debugName = "test indices"},
                              error);
  assert(indexBuffer);

  struct Vertex {
    float x;
    float y;
    float z;
    std::uint32_t color;
  };
  const std::array<Vertex, 3> vertices{};
  const auto vertexBytes =
      std::as_bytes(std::span<const Vertex>(vertices.data(), vertices.size()));
  const BufferHandle vertexBuffer = resources->createBuffer(
      BufferCreateInfo{
          .kind = BufferKind::Vertex,
          .data = vertexBytes,
          .vertexLayout =
              VertexLayout{.attributes =
                               {
                                   {.semantic = VertexSemantic::Position,
                                    .components = 3,
                                    .type = VertexElementType::Float},
                                   {.semantic = VertexSemantic::Color0,
                                    .components = 4,
                                    .type = VertexElementType::UInt8,
                                    .normalized = true},
                               }},
          .debugName = "test vertices"},
      error);
  assert(vertexBuffer);
  const BufferHandle dynamicVertexBuffer = resources->createBuffer(
      BufferCreateInfo{
          .kind = BufferKind::DynamicVertex,
          .data = vertexBytes,
          .vertexLayout =
              VertexLayout{.attributes =
                               {
                                   {.semantic = VertexSemantic::Position,
                                    .components = 3,
                                    .type = VertexElementType::Float},
                                   {.semantic = VertexSemantic::Color0,
                                    .components = 4,
                                    .type = VertexElementType::UInt8,
                                    .normalized = true},
                               }},
          .debugName = "dynamic test vertices"},
      error);
  assert(dynamicVertexBuffer);
  assert(resources->updateBuffer(dynamicVertexBuffer, vertexBytes, error));

  const BufferHandle invalidVertexBuffer =
      resources->createBuffer(BufferCreateInfo{.kind = BufferKind::Vertex,
                                               .data = vertexBytes,
                                               .vertexLayout = {},
                                               .debugName = "invalid vertices"},
                              error);
  assert(!invalidVertexBuffer);
  assert(error.find("attribute") != std::string::npos);

  const BufferHandle duplicateSemanticBuffer = resources->createBuffer(
      BufferCreateInfo{
          .kind = BufferKind::Vertex,
          .data = vertexBytes,
          .vertexLayout =
              VertexLayout{.attributes =
                               {
                                   {.semantic = VertexSemantic::Position,
                                    .components = 3},
                                   {.semantic = VertexSemantic::Position,
                                    .components = 1},
                               }},
          .debugName = "duplicate semantics"},
      error);
  assert(!duplicateSemanticBuffer);
  assert(error.find("duplicate") != std::string::npos);

  const RenderTargetHandles target = resources->createRenderTarget(
      RenderTargetCreateInfo{
          .width = 16, .height = 16, .debugName = "test target"},
      error);
  assert(target.frameBuffer);
  assert(target.color);
  assert(target.depth);
  assert(target.msaaSamples==1);
  for(int samples:{0,1,2,4,8,16}) {
    const auto multisample=resources->createRenderTarget({.width=32,.height=32,.debugName="MSAA target",.msaaSamples=samples},error);
    assert(multisample.frameBuffer && multisample.color && multisample.depth);
    assert(multisample.msaaSamples==1 || multisample.msaaSamples==samples);
    assert(resources->destroy(multisample.frameBuffer));
    assert(resources->destroy(multisample.color));
    assert(resources->destroy(multisample.depth));
  }
  assert(!resources->createRenderTarget({.msaaSamples=3},error).frameBuffer);
  assert(!resources->createRenderTarget({.msaaSamples=-1},error).frameBuffer);
  assert(!resources->createTexture({.msaaSamples=4},error));
  const auto colorOnly=resources->createRenderTarget({.width=16,.height=16,.depth=false,.msaaSamples=4},error);
  assert(colorOnly.frameBuffer && colorOnly.color && !colorOnly.depth);
  assert(resources->destroy(colorOnly.frameBuffer));
  assert(resources->destroy(colorOnly.color));

  assert(resources->destroy(indexBuffer));
  assert(!resources->destroy(indexBuffer));
  assert(resources->destroy(vertexBuffer));
  assert(resources->destroy(dynamicVertexBuffer));
  assert(resources->destroy(target.frameBuffer));
  assert(resources->destroy(target.depth));
  assert(resources->destroy(target.color));
  assert(resources->destroy(sampler));
  assert(resources->destroy(texture));
  resources->clear();
  graphics.shutdown();
  return 0;
}
