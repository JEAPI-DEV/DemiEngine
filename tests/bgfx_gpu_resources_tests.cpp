#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/GpuResources.h"

#include <array>
#include <bgfx/bgfx.h>
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
  const auto limits = resources->limits();
  const auto *nativeCaps = bgfx::getCaps();
  assert(nativeCaps != nullptr);
  assert(limits.maxTextureSize == nativeCaps->limits.maxTextureSize);
  assert(limits.maxTextureSamplers == nativeCaps->limits.maxTextureSamplers);
  assert(limits.supportsInstancing ==
         ((nativeCaps->supported & BGFX_CAPS_INSTANCING) != 0));
  for (const auto &[format, nativeFormat] : std::array{
           std::pair{TextureFormat::R32F, bgfx::TextureFormat::R32F},
           std::pair{TextureFormat::RGBA32F, bgfx::TextureFormat::RGBA32F},
           std::pair{TextureFormat::RGBA8, bgfx::TextureFormat::RGBA8}}) {
    const auto capabilities = resources->textureCapabilities(format);
    const auto supported = nativeCaps->formats[nativeFormat];
    assert(capabilities.sampled2D ==
           ((supported & (BGFX_CAPS_FORMAT_TEXTURE_2D |
                          BGFX_CAPS_FORMAT_TEXTURE_2D_EMULATED)) != 0));
    assert(capabilities.vertexSampling ==
           ((supported & BGFX_CAPS_FORMAT_TEXTURE_VERTEX) != 0));
  }
  assert(!resources->textureCapabilities(static_cast<TextureFormat>(-1))
              .sampled2D);

  const std::array<float, 4> heights{1.0F, 2.0F, 3.0F, 4.0F};
  const auto heightBytes = std::as_bytes(std::span(heights));
  const auto heightTexture =
      resources->createTexture({.width = 2,
                                .height = 2,
                                .format = TextureFormat::R32F,
                                .filter = TextureFilter::Nearest},
                               error);
  assert(heightTexture);
  assert(resources->updateTexture(
      heightTexture, {.width = 2, .height = 2, .data = heightBytes}, error));
  const std::array<float, 1> patch{5.0F};
  assert(resources->updateTexture(heightTexture,
                                  {.x = 1,
                                   .y = 1,
                                   .width = 1,
                                   .height = 1,
                                   .data = std::as_bytes(std::span(patch))},
                                  error));
  assert(!resources->updateTexture(
      heightTexture, {.width = 1, .height = 1, .data = heightBytes}, error));
  assert(!resources->updateTexture(heightTexture,
                                   {.x = 2,
                                    .width = 1,
                                    .height = 1,
                                    .data = std::as_bytes(std::span(patch))},
                                   error));
  assert(!resources->createTexture({.width = 2,
                                    .height = 2,
                                    .format = TextureFormat::R32F,
                                    .data = std::as_bytes(std::span(patch))},
                                   error));
  const auto immutableHeightTexture =
      resources->createTexture({.width = 2,
                                .height = 2,
                                .format = TextureFormat::R32F,
                                .data = heightBytes},
                               error);
  assert(immutableHeightTexture);
  assert(!resources->updateTexture(
      immutableHeightTexture, {.width = 2, .height = 2, .data = heightBytes},
      error));
  const std::array<float, 5> heightMips{};
  const auto mipTexture =
      resources->createTexture({.width = 2,
                                .height = 2,
                                .format = TextureFormat::R32F,
                                .data = std::as_bytes(std::span(heightMips)),
                                .generateMipmaps = true},
                               error);
  assert(mipTexture);
  assert(!resources->createTexture({.width = 2,
                                    .height = 2,
                                    .format = TextureFormat::R32F,
                                    .data = heightBytes,
                                    .generateMipmaps = true},
                                   error));
  assert(resources->destroy(mipTexture));
  assert(resources->destroy(immutableHeightTexture));
  assert(resources->destroy(heightTexture));
  assert(!resources->updateTexture(
      heightTexture, {.width = 2, .height = 2, .data = heightBytes}, error));

  const std::array<std::byte, 16> pixels{};
  const TextureHandle texture =
      resources->createTexture(TextureCreateInfo{.width = 2,
                                                 .height = 2,
                                                 .format = TextureFormat::RGBA8,
                                                 .data = pixels,
                                                 .debugName = "test texture"},
                               error);
  assert(texture);
  const std::array<float, 16> lightData{1, 2, 3, 8, 1, .5F, 0, 2};
  const auto lightBytes = std::as_bytes(std::span(lightData));
  const auto lights = resources->createTexture(
      {.width = 4, .height = 1, .format = TextureFormat::RGBA32F}, error);
  assert(lights);
  assert(resources->updateTexture(
      lights, {.width = 4, .height = 1, .data = lightBytes}, error));
  assert(!resources->updateTexture(
      lights, {.width = 4, .height = 1, .data = pixels}, error));
  assert(!resources->updateTexture(
      lights, {.x = 1, .width = 4, .height = 1, .data = lightBytes}, error));
  assert(resources->destroy(lights));
  const auto immutable =
      resources->createTexture({.width = 4,
                                .height = 1,
                                .format = TextureFormat::RGBA32F,
                                .data = lightBytes},
                               error);
  assert(immutable);
  assert(!resources->updateTexture(
      immutable, {.width = 4, .height = 1, .data = lightBytes}, error));
  assert(resources->destroy(immutable));

  for (const auto builtin :
       {BuiltinProgram::Terrain3D, BuiltinProgram::Terrain3DDirectional}) {
    const auto program = resources->createBuiltinProgram(builtin, error);
    assert(program);
    assert(resources->destroy(program));
  }

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
  assert(target.msaaSamples == 1);
  for (int samples : {0, 1, 2, 4, 8, 16}) {
    const auto multisample =
        resources->createRenderTarget({.width = 32,
                                       .height = 32,
                                       .debugName = "MSAA target",
                                       .msaaSamples = samples},
                                      error);
    assert(multisample.frameBuffer && multisample.color && multisample.depth);
    assert(multisample.msaaSamples == 1 || multisample.msaaSamples == samples);
    assert(resources->destroy(multisample.frameBuffer));
    assert(resources->destroy(multisample.color));
    assert(resources->destroy(multisample.depth));
  }
  assert(!resources->createRenderTarget({.msaaSamples = 3}, error).frameBuffer);
  assert(
      !resources->createRenderTarget({.msaaSamples = -1}, error).frameBuffer);
  assert(!resources->createTexture({.msaaSamples = 4}, error));
  const auto colorOnly = resources->createRenderTarget(
      {.width = 16, .height = 16, .depth = false, .msaaSamples = 4}, error);
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
