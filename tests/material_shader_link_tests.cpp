#include "demi/runtime/render/PerformanceDebug.h"
#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/GpuResources.h"

#include <cstdint>

#include "fs_demi_directional_mrt_spv.h"
#include "fs_demi_linear_depth_resolve_spv.h"
#include "fs_demi_lit_mrt_spv.h"
#include "fs_demi_pbr_directional_mrt_spv.h"
#include "fs_demi_pbr_mrt_spv.h"
#include "fs_demi_sky_mrt_spv.h"
#include "fs_demi_terrain_directional_mrt_spv.h"
#include "fs_demi_terrain_mapped_directional_mrt_spv.h"
#include "fs_demi_terrain_mapped_mrt_spv.h"
#include "fs_demi_terrain_mapped_opaque_directional_mrt_spv.h"
#include "fs_demi_terrain_mapped_opaque_mrt_spv.h"
#include "fs_demi_terrain_mrt_spv.h"
#include "vs_demi_lit_instanced_mrt_spv.h"
#include "vs_demi_lit_mrt_spv.h"
#include "vs_demi_post_process_spv.h"
#include "vs_demi_skinned_mrt_spv.h"
#include "vs_demi_sky_mrt_spv.h"

#include "fs_demi_pbr_directional_mrt_essl.h"
#include "fs_demi_pbr_directional_mrt_glsl.h"
#include "fs_demi_pbr_mrt_essl.h"
#include "fs_demi_pbr_mrt_glsl.h"

#include "fs_demi_directional_spv.h"
#include "fs_demi_lit_spv.h"
#include "fs_demi_pbr_directional_essl.h"
#include "fs_demi_pbr_directional_glsl.h"
#include "fs_demi_pbr_directional_spv.h"
#include "fs_demi_pbr_essl.h"
#include "fs_demi_pbr_glsl.h"
#include "fs_demi_pbr_spv.h"
#include "fs_demi_terrain_directional_spv.h"
#include "fs_demi_terrain_mapped_directional_essl.h"
#include "fs_demi_terrain_mapped_directional_glsl.h"
#include "fs_demi_terrain_mapped_directional_spv.h"
#include "fs_demi_terrain_mapped_essl.h"
#include "fs_demi_terrain_mapped_glsl.h"
#include "fs_demi_terrain_mapped_opaque_directional_essl.h"
#include "fs_demi_terrain_mapped_opaque_directional_glsl.h"
#include "fs_demi_terrain_mapped_opaque_directional_spv.h"
#include "fs_demi_terrain_mapped_opaque_essl.h"
#include "fs_demi_terrain_mapped_opaque_glsl.h"
#include "fs_demi_terrain_mapped_opaque_spv.h"
#include "fs_demi_terrain_mapped_spv.h"
#include "fs_demi_terrain_spv.h"
#include "fs_demi_water_essl.h"
#include "fs_demi_water_glsl.h"
#include "fs_demi_water_spv.h"
#include "vs_demi_lit_instanced_spv.h"
#include "vs_demi_lit_spv.h"
#include "vs_demi_skinned_spv.h"
#include "vs_demi_terrain_spv.h"
#include "vs_demi_water_spv.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace demi::runtime::render;

namespace {
enum class ShaderCode { Spirv, Glsl };

struct Shader {
  const char *name;
  std::span<const std::byte> bytes;
  ShaderCode code = ShaderCode::Spirv;
};

template <std::size_t Size>
Shader shader(const char *name, const std::uint8_t (&bytes)[Size],
              ShaderCode code = ShaderCode::Spirv) {
  return {name, std::as_bytes(std::span(bytes)), code};
}
struct CompiledShaderInterface {
  std::vector<std::string_view> samplers;
  std::span<const std::byte> code;
};

CompiledShaderInterface readCompiledShaderInterface(const Shader &shader) {
  // Read the pinned bgfx binary uniform table and its compiler-output payload.
  // Linked varying hashes alone miss lost build defines.
  const auto bytes = shader.bytes;
  if (bytes.size() < 14 || bytes[3] != std::byte{11}) {
    throw std::runtime_error(std::string(shader.name) +
                             ": unexpected bgfx shader header");
  }
  const auto uniformCount = std::to_integer<unsigned>(bytes[12]) |
                            (std::to_integer<unsigned>(bytes[13]) << 8);
  std::size_t cursor = 14;
  std::vector<std::string_view> samplers;
  for (unsigned uniform = 0; uniform < uniformCount; ++uniform) {
    if (cursor >= bytes.size()) {
      throw std::runtime_error(std::string(shader.name) +
                               ": truncated uniform table");
    }
    const auto nameSize = std::to_integer<unsigned>(bytes[cursor]);
    ++cursor;
    // Type/count/register/count/component/dimension/format occupy ten bytes.
    if (nameSize + 10 > bytes.size() - cursor) {
      throw std::runtime_error(std::string(shader.name) +
                               ": truncated uniform metadata");
    }
    const std::string_view name(
        reinterpret_cast<const char *>(bytes.data() + cursor), nameSize);
    cursor += nameSize;
    const auto type = std::to_integer<unsigned>(bytes[cursor]);
    constexpr unsigned samplerType = 0;
    constexpr unsigned typeMask = 0x0f;
    if ((type & typeMask) == samplerType) {
      samplers.push_back(name);
    }
    cursor += 10;
  }
  if (bytes.size() - cursor < 4) {
    throw std::runtime_error(std::string(shader.name) +
                             ": missing compiled shader payload");
  }
  std::uint32_t codeSize = 0;
  for (unsigned byte = 0; byte < 4; ++byte) {
    codeSize |= std::to_integer<std::uint32_t>(bytes[cursor + byte])
                << (8 * byte);
  }
  cursor += 4;
  if (codeSize > bytes.size() - cursor) {
    throw std::runtime_error(std::string(shader.name) +
                             ": truncated compiled shader payload");
  }
  return {std::move(samplers), bytes.subspan(cursor, codeSize)};
}

void requireEarlyPbrCoverage(const Shader &shader) {
  const auto compiled = readCompiledShaderInterface(shader);
  const std::string_view code(
      reinterpret_cast<const char *>(compiled.code.data()),
      compiled.code.size());
  const auto coverageDiscard = code.find("discard");
  if (coverageDiscard == std::string_view::npos) {
    throw std::runtime_error(std::string(shader.name) +
                             ": PBR alpha coverage is missing");
  }
  // Inspect the actual optimized production GLSL/ESSL, rather than a duplicate
  // CPU shader. All expensive shading-map reads must follow coverage rejection.
  for (const auto sampler :
       {"s_pbrNormal", "s_pbrRoughness", "s_pbrMetallic", "s_pbrOcclusion",
        "s_pbrDetailNormal", "s_pbrEmissive"}) {
    const auto sampledUse = "(" + std::string(sampler) + ",";
    const auto sample = code.find(sampledUse);
    if (sample == std::string_view::npos || sample < coverageDiscard) {
      throw std::runtime_error(
          std::string(shader.name) +
          ": shading map sampled before coverage: " + sampler);
    }
  }
  // Derivatives evaluated after discard would depend on inactive quad lanes,
  // breaking normal frames or mip selection at the cutout's visible edge.
  if (code.find("dFdx", coverageDiscard) != std::string_view::npos ||
      code.find("dFdy", coverageDiscard) != std::string_view::npos) {
    throw std::runtime_error(
        std::string(shader.name) +
        ": derivatives evaluated after coverage rejection");
  }
  if (code.find("texture2D (", coverageDiscard) != std::string_view::npos ||
      code.find("texture2D(", coverageDiscard) != std::string_view::npos) {
    throw std::runtime_error(
        std::string(shader.name) +
        ": implicit texture LOD evaluated after coverage rejection");
  }
  if (code.find("texture2DGrad") == std::string_view::npos &&
      code.find("textureGrad") == std::string_view::npos) {
    throw std::runtime_error(std::string(shader.name) +
                             ": explicit material gradients are missing");
  }
}

void requireMappedArraySamplers(const Shader &shader) {
  const auto compiled = readCompiledShaderInterface(shader);
  constexpr std::array<std::string_view, 5> expected{
      "s_terrainAlbedo", "s_terrainNormals", "s_terrainOrmHeight",
      "s_terrainDetail", "s_terrainEmissive"};
  for (const auto name : expected) {
    bool expectsSampler = true;
#if DEMI_PERFORMANCE_DEBUG && DEMI_PERFORMANCE_DEBUG_DISABLE_TERRAIN_MATERIALS
    expectsSampler = false;
#elif DEMI_PERFORMANCE_DEBUG && DEMI_PERFORMANCE_DEBUG_DISABLE_TERRAIN_NORMALS
    expectsSampler = name != "s_terrainNormals" && name != "s_terrainDetail";
#endif
    bool hasSampler = false;
    if (shader.code == ShaderCode::Spirv) {
      hasSampler = std::find(compiled.samplers.begin(), compiled.samplers.end(),
                             name) != compiled.samplers.end();
    } else {
      // bgfx GLSL binaries have no serialized uniform table: the GL driver
      // reflects their embedded compiler output. Require both the array type
      // and a sampled use, rather than merely finding a declaration name.
      const std::string_view code(
          reinterpret_cast<const char *>(compiled.code.data()),
          compiled.code.size());
      const auto declaration =
          "uniform sampler2DArray " + std::string(name) + ";";
      const auto sampledUse = "(" + std::string(name) + ",";
      hasSampler = code.find(declaration) != std::string_view::npos &&
                   code.find(sampledUse) != std::string_view::npos;
    }
    if (hasSampler != expectsSampler) {
      throw std::runtime_error(std::string(shader.name) +
                               ": unexpected compiled array sampler state " +
                               std::string(name));
    }
  }
#if DEMI_PERFORMANCE_DEBUG && DEMI_PERFORMANCE_DEBUG_DISABLE_SHADOW_RECEIVE
  if (shader.code == ShaderCode::Spirv &&
      std::find(compiled.samplers.begin(), compiled.samplers.end(),
                "s_shadowMap") != compiled.samplers.end()) {
    throw std::runtime_error(std::string(shader.name) +
                             ": disabled shadow filtering retains its sampler");
  }
#endif
}

void requireOpaqueCoverage(const Shader &shader) {
  const auto compiled = readCompiledShaderInterface(shader);
  if (shader.code == ShaderCode::Glsl) {
    const std::string_view code(
        reinterpret_cast<const char *>(compiled.code.data()),
        compiled.code.size());
    if (code.find("discard") != std::string_view::npos) {
      throw std::runtime_error(std::string(shader.name) +
                               ": opaque variant can discard fragments");
    }
    return;
  }
  const auto word = [&](std::size_t index) {
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte) {
      value |= std::to_integer<std::uint32_t>(compiled.code[index * 4 + byte])
               << (byte * 8);
    }
    return value;
  };
  if (compiled.code.size() < 20 || compiled.code.size() % 4 != 0 ||
      word(0) != 0x07230203U) {
    throw std::runtime_error(std::string(shader.name) +
                             ": invalid SPIR-V payload");
  }
  // Walk instruction headers, not arbitrary payload words which may share an
  // opcode's numeric value. Reject both terminating and demoting discards.
  const auto words = compiled.code.size() / 4;
  for (std::size_t cursor = 5; cursor < words;) {
    const auto instruction = word(cursor);
    const auto length = instruction >> 16;
    const auto opcode = instruction & 0xffffU;
    if (length == 0 || length > words - cursor) {
      throw std::runtime_error(std::string(shader.name) +
                               ": invalid SPIR-V instruction");
    }
    constexpr unsigned kill = 252;
    constexpr unsigned terminateInvocation = 4416;
    constexpr unsigned demoteToHelperInvocation = 5380;
    if (opcode == kill || opcode == terminateInvocation ||
        opcode == demoteToHelperInvocation) {
      throw std::runtime_error(std::string(shader.name) +
                               ": opaque variant can discard fragments");
    }
    cursor += length;
  }
}

void requireAllocatedSampleQuery(const Shader &shader) {
  const auto compiled = readCompiledShaderInterface(shader);
  const auto word = [&](std::size_t index) {
    std::uint32_t value = 0;
    for (unsigned byte = 0; byte < 4; ++byte) {
      value |= std::to_integer<std::uint32_t>(compiled.code[index * 4 + byte])
               << (byte * 8);
    }
    return value;
  };
  assert(compiled.code.size() >= 20 && compiled.code.size() % 4 == 0);
  assert(word(0) == 0x07230203U);
  bool queriesAllocatedSamples = false;
  for (std::size_t cursor = 5; cursor < compiled.code.size() / 4;) {
    const auto instruction = word(cursor);
    const auto length = instruction >> 16;
    assert(length > 0 && length <= compiled.code.size() / 4 - cursor);
    // SPIR-V OpImageQuerySamples: never trust the requested CPU MSAA count
    // after the backend has selected the actual image allocation mode.
    queriesAllocatedSamples |= (instruction & 0xffffU) == 107;
    cursor += length;
  }
  assert(queriesAllocatedSamples);
}

} // namespace

int main() {
  for (const auto &fragment :
       {shader("pbr_glsl", fs_demi_pbr_glsl, ShaderCode::Glsl),
        shader("pbr_essl", fs_demi_pbr_essl, ShaderCode::Glsl),
        shader("pbr_directional_glsl", fs_demi_pbr_directional_glsl,
               ShaderCode::Glsl),
        shader("pbr_directional_essl", fs_demi_pbr_directional_essl,
               ShaderCode::Glsl),
        shader("MRT pbr_glsl", fs_demi_pbr_mrt_glsl, ShaderCode::Glsl),
        shader("MRT pbr_essl", fs_demi_pbr_mrt_essl, ShaderCode::Glsl),
        shader("MRT pbr_directional_glsl", fs_demi_pbr_directional_mrt_glsl,
               ShaderCode::Glsl),
        shader("MRT pbr_directional_essl", fs_demi_pbr_directional_mrt_essl,
               ShaderCode::Glsl)}) {
    requireEarlyPbrCoverage(fragment);
  }

  for (const auto &compiled :
       {shader("water Vulkan", fs_demi_water_spv),
        shader("water OpenGL", fs_demi_water_glsl, ShaderCode::Glsl),
        shader("water OpenGLES", fs_demi_water_essl, ShaderCode::Glsl)}) {
    const auto interface = readCompiledShaderInterface(compiled);
    for (const std::string_view sampler :
         {"s_waterSceneColor", "s_waterSceneDepth", "s_waterEnvironment"}) {
      bool present = false;
      if (compiled.code == ShaderCode::Spirv) {
        present =
            std::find(interface.samplers.begin(), interface.samplers.end(),
                      sampler) != interface.samplers.end();
      } else {
        const std::string_view code(
            reinterpret_cast<const char *>(interface.code.data()),
            interface.code.size());
        present = code.find("(" + std::string(sampler) + ",") !=
                  std::string_view::npos;
      }
      assert(present);
    }
  }
  for (const auto &compiled :
       {shader("mapped Vulkan", fs_demi_terrain_mapped_spv),
        shader("mapped directional Vulkan",
               fs_demi_terrain_mapped_directional_spv),
        shader("mapped OpenGL", fs_demi_terrain_mapped_glsl, ShaderCode::Glsl),
        shader("mapped directional OpenGL",
               fs_demi_terrain_mapped_directional_glsl, ShaderCode::Glsl),
        shader("mapped OpenGLES", fs_demi_terrain_mapped_essl,
               ShaderCode::Glsl),
        shader("mapped directional OpenGLES",
               fs_demi_terrain_mapped_directional_essl, ShaderCode::Glsl)}) {
    requireMappedArraySamplers(compiled);
  }
  for (const auto &compiled :
       {shader("opaque mapped Vulkan", fs_demi_terrain_mapped_opaque_spv),
        shader("opaque mapped directional Vulkan",
               fs_demi_terrain_mapped_opaque_directional_spv),
        shader("opaque mapped OpenGL", fs_demi_terrain_mapped_opaque_glsl,
               ShaderCode::Glsl),
        shader("opaque mapped directional OpenGL",
               fs_demi_terrain_mapped_opaque_directional_glsl,
               ShaderCode::Glsl),
        shader("opaque mapped OpenGLES", fs_demi_terrain_mapped_opaque_essl,
               ShaderCode::Glsl),
        shader("opaque mapped directional OpenGLES",
               fs_demi_terrain_mapped_opaque_directional_essl,
               ShaderCode::Glsl)}) {
    requireMappedArraySamplers(compiled);
    requireOpaqueCoverage(compiled);
  }

  BgfxGraphicsDevice graphics;
  std::string error;
  assert(graphics.initialize(
      {.api = GraphicsApi::Noop, .width = 64, .height = 64, .vsync = false},
      error));
  auto resources = createBgfxGpuResources();
  // bgfx retires destroyed uniform handles asynchronously at frame boundaries.
  // Keep the entire link matrix alive together so shared uniform names remain
  // referenced, rather than exhausting the retired-handle pool without frames.
  std::vector<ProgramHandle> linkedPrograms;
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
      linkedPrograms.push_back(program);
      ++linked;
    }
  }
  assert(linked == 12);
  const auto terrainVertex = shader("terrain", vs_demi_terrain_spv);
  for (const auto &fragment :
       {shader("terrain", fs_demi_terrain_spv),
        shader("terrain_directional", fs_demi_terrain_directional_spv),
        shader("terrain_mapped", fs_demi_terrain_mapped_spv),
        shader("terrain_mapped_directional",
               fs_demi_terrain_mapped_directional_spv),
        shader("terrain_mapped_opaque", fs_demi_terrain_mapped_opaque_spv),
        shader("terrain_mapped_opaque_directional",
               fs_demi_terrain_mapped_opaque_directional_spv)}) {
    const auto program =
        resources->createProgram({.vertexShader = terrainVertex.bytes,
                                  .fragmentShader = fragment.bytes,
                                  .debugName = fragment.name},
                                 error);
    if (!program) {
      std::cerr << fragment.name << ": " << error << '\n';
    }
    assert(program);
    linkedPrograms.push_back(program);
    ++linked;
  }
  assert(linked == 18);
  const auto waterVertex = shader("water", vs_demi_water_spv);
  const auto waterFragment = shader("water", fs_demi_water_spv);
  const auto waterProgram =
      resources->createProgram({.vertexShader = waterVertex.bytes,
                                .fragmentShader = waterFragment.bytes,
                                .debugName = "water surface"},
                               error);
  assert(waterProgram);
  linkedPrograms.push_back(waterProgram);
  ++linked;
  // Real generated MRT interfaces must link across every supported geometry
  // family. Noop builtin placeholders cannot validate varying contracts.
  const std::array mrtVertices{
      shader("MRT lit", vs_demi_lit_mrt_spv),
      shader("MRT instanced", vs_demi_lit_instanced_mrt_spv),
      shader("MRT skinned", vs_demi_skinned_mrt_spv)};
  const std::array mrtFragments{
      shader("MRT lit", fs_demi_lit_mrt_spv),
      shader("MRT directional", fs_demi_directional_mrt_spv),
      shader("MRT PBR", fs_demi_pbr_mrt_spv),
      shader("MRT PBR directional", fs_demi_pbr_directional_mrt_spv)};
  const auto link = [&](const Shader &vertex, const Shader &fragment) {
    const auto program =
        resources->createProgram({.vertexShader = vertex.bytes,
                                  .fragmentShader = fragment.bytes,
                                  .debugName = fragment.name},
                                 error);
    if (!program) {
      std::cerr << fragment.name << ": " << error << '\n';
    }
    assert(program);
    linkedPrograms.push_back(program);
  };
  for (const auto &vertex : mrtVertices) {
    for (const auto &fragment : mrtFragments) {
      link(vertex, fragment);
    }
  }
  for (const auto &fragment :
       {shader("MRT terrain", fs_demi_terrain_mrt_spv),
        shader("MRT directional terrain", fs_demi_terrain_directional_mrt_spv),
        shader("MRT mapped terrain", fs_demi_terrain_mapped_mrt_spv),
        shader("MRT directional mapped terrain",
               fs_demi_terrain_mapped_directional_mrt_spv),
        shader("MRT opaque mapped terrain",
               fs_demi_terrain_mapped_opaque_mrt_spv),
        shader("MRT directional opaque mapped terrain",
               fs_demi_terrain_mapped_opaque_directional_mrt_spv)}) {
    link(terrainVertex, fragment);
  }
  link(shader("MRT sky", vs_demi_sky_mrt_spv),
       shader("MRT sky", fs_demi_sky_mrt_spv));
  const auto resolveFragment =
      shader("nearest MSAA depth", fs_demi_linear_depth_resolve_spv);
  requireAllocatedSampleQuery(resolveFragment);
  const auto resolveInterface = readCompiledShaderInterface(resolveFragment);
  assert(std::ranges::find(resolveInterface.samplers, "s_opaqueDepthMsaa") !=
         resolveInterface.samplers.end());
  link(shader("depth resolve quad", vs_demi_post_process_spv), resolveFragment);
  for (const auto program : linkedPrograms) {
    resources->destroy(program);
  }
  resources.reset();
  graphics.shutdown();
  assert(linked == 19);
  std::cout << "All 19 ordinary and 20 MRT/resolve shader interfaces link\n";
}
