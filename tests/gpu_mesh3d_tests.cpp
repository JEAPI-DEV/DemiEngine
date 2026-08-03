#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/RenderCommands.h"
#include "demi/runtime/render/bgfx3d/GpuMesh3D.h"

#include <array>
#include <cassert>
#include <string>
#include <vector>

using namespace demi::runtime;
using namespace demi::runtime::render;

int main() {
  BgfxGraphicsDevice graphics;
  std::string error;
  assert(graphics.initialize(
      {.api = GraphicsApi::Noop, .width = 64, .height = 64, .vsync = false},
      error));
  auto resources = createBgfxGpuResources();
  auto commands = createBgfxRenderCommands(*resources);
  const ProgramHandle program =
      resources->createBuiltinProgram(BuiltinProgram::Textured3D, error);
  const SamplerHandle sampler = resources->createSampler("s_texColor", error);
  const std::array<std::byte, 4> white{std::byte{0xff}, std::byte{0xff},
                                       std::byte{0xff}, std::byte{0xff}};
  const TextureHandle texture =
      resources->createTexture({.data = white, .debugName = "white"}, error);
  assert(program && sampler && texture);

  GpuMesh3D mesh(*resources);
  constexpr std::array<Vec3, 3> Positions{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}};
  constexpr std::array<Vec2, 2> WrongUvs{{{0, 0}, {1, 0}}};
  constexpr std::array<std::uint32_t, 3> BadIndices{{0, 1, 4}};
  constexpr std::array<std::uint32_t, 3> Indices{{0, 1, 2}};
  constexpr std::array<std::uint32_t, 2> WrongColors{{0xffffffffU,
                                                       0xffffffffU}};
  constexpr std::array<std::uint32_t, 3> Colors{{0xff0000ffU, 0x00ff00ffU,
                                                  0x0000ffffU}};
  assert(!mesh.upload({}, {}, {}, 0xffffffffU, error));
  assert(!mesh.upload(Positions, WrongUvs, {}, 0xffffffffU, error));
  assert(!mesh.upload(Positions, {}, BadIndices, 0xffffffffU, error));
  assert(!mesh.upload(Positions, {}, Indices, 0xffffffffU, error, {},
                      WrongColors));
  assert(mesh.upload(Positions, {}, Indices, 0xffffffffU, error, {}, Colors));
  assert(mesh.vertexCount() == 3 && mesh.indexCount() == 3);
  assert(commands->configureView3D({.width = 64, .height = 64}, error));
  assert(mesh.draw(*commands, 0, program, texture, sampler, {},
                   {.depthTest = DepthTest::Less, .writeDepth = true}, error));
  static_cast<void>(graphics.endFrame());

  // Exercise the 32-bit index path at the first size that cannot fit in a
  // uint16_t buffer, including indices at both extremes.
  std::vector<Vec3> large(65536);
  const std::array<std::uint32_t, 3> largeIndices{{0, 32768, 65535}};
  assert(mesh.upload(large, {}, largeIndices, 0xffffffffU, error));
  assert(mesh.vertexCount() == 65536);
  mesh.clear();
  mesh.clear();
  assert(!mesh.draw(*commands, 0, program, texture, sampler, {}, {}, error));

  resources->clear();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
