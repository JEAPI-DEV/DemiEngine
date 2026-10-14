#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/GpuResources.h"

#include <cstdint>

#include "fs_demi_directional_spv.h"
#include "fs_demi_lit_spv.h"
#include "fs_demi_pbr_directional_spv.h"
#include "fs_demi_pbr_spv.h"
#include "fs_demi_terrain_directional_spv.h"
#include "fs_demi_terrain_spv.h"
#include "vs_demi_lit_instanced_spv.h"
#include "vs_demi_lit_spv.h"
#include "vs_demi_skinned_spv.h"
#include "vs_demi_terrain_spv.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <array>
#include <cassert>
#include <iostream>
#include <span>
#include <string>

using namespace demi::runtime::render;

namespace {
struct Shader {
  const char *name;
  std::span<const std::byte> bytes;
};

template <std::size_t Size>
Shader shader(const char *name, const std::uint8_t (&bytes)[Size]) {
  return {name, std::as_bytes(std::span(bytes))};
}
} // namespace

int main() {
  BgfxGraphicsDevice graphics;
  std::string error;
  assert(graphics.initialize(
      {.api = GraphicsApi::Noop, .width = 64, .height = 64, .vsync = false},
      error));
  auto resources = createBgfxGpuResources();
  const std::array vertices{shader("lit", vs_demi_lit_spv),
                            shader("instanced", vs_demi_lit_instanced_spv),
                            shader("skinned", vs_demi_skinned_spv)};
  const std::array fragments{
      shader("lit", fs_demi_lit_spv),
      shader("directional", fs_demi_directional_spv),
      shader("pbr", fs_demi_pbr_spv),
      shader("pbr_directional", fs_demi_pbr_directional_spv)};

  // Noop ignores SPIR-V execution, but bgfx's common shader/program creation
  // still parses the real generated varying hashes and rejects interface
  // mismatches. Builtin Noop placeholder programs cannot cover this contract.
  std::size_t linked = 0;
  for (const auto &vertex : vertices) {
    for (const auto &fragment : fragments) {
      const std::string name = std::string(vertex.name) + "/" + fragment.name;
      const auto program =
          resources->createProgram({.vertexShader = vertex.bytes,
                                    .fragmentShader = fragment.bytes,
                                    .debugName = name},
                                   error);
      if (!program) {
        std::cerr << name << ": " << error << '\n';
      }
      assert(program);
      resources->destroy(program);
      ++linked;
    }
  }
  assert(linked == 12);
  const auto terrainVertex = shader("terrain", vs_demi_terrain_spv);
  for (const auto &fragment :
       {shader("terrain", fs_demi_terrain_spv),
        shader("terrain_directional", fs_demi_terrain_directional_spv)}) {
    const auto program =
        resources->createProgram({.vertexShader = terrainVertex.bytes,
                                  .fragmentShader = fragment.bytes,
                                  .debugName = fragment.name},
                                 error);
    if (!program) {
      std::cerr << fragment.name << ": " << error << '\n';
    }
    assert(program);
    resources->destroy(program);
    ++linked;
  }
  assert(linked == 14);
  resources.reset();
  graphics.shutdown();
  std::cout << "All 14 generated mesh and terrain shader interfaces link\n";
}
