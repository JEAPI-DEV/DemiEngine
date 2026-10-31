#pragma once

#include "demi/runtime/render/backend/RenderCommands.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime {
struct HeightField;
namespace render {
struct MaterialBinding;
inline constexpr std::size_t terrainMappedPaletteStride3D = 6;

using TerrainGpuMaterialResolver3D =
    std::function<const MaterialBinding *(std::string_view)>;

// CPU-only texture payloads. Unsupported content returns an empty result with a
// reason, so the ordinary material renderer remains the whole-field fallback.
struct TerrainGpuFieldData3D {
  bool compatible = false;
  bool mapped = false;
  // Mixed None/CW palette entries need categorical fragment culling. A uniform
  // cull policy is fully enforced by the shared raster state instead.
  bool requiresFragmentCulling = true;
  // Sorted unique IDs, including the neutral empty ID at layer zero.
  std::vector<std::string> materialIds;
  std::string fallbackReason;
  DrawState state;
  std::uint16_t sampleWidth = 0;
  std::uint16_t sampleHeight = 0;
  std::uint16_t paletteWidth = 0;
  std::uint16_t paletteHeight = 0;
  std::size_t paletteEntries = 0;
  // Ordinary fields retain R32F. Transition fields pack final/from heights in
  // RGBA32F so one shader can interpolate without another texture sampler.
  TextureFormat heightFormat = TextureFormat::R32F;
  std::vector<float> heights;
  std::vector<std::array<float, 4>> normals;
  // An exact unsigned palette index in little-endian RGBA bytes.
  std::vector<std::array<std::uint8_t, 4>> materialIndices;
  // Row-major RGBA32F entries start with linear tinted RGB/unchanged alpha,
  // then metallic/roughness/unlit/CW-cull. Mapped entries add three surface
  // records and a precise projection phase (six texels total); solid entries use two. Tail texels are zero.
  std::vector<std::array<float, 4>> palette;
};

[[nodiscard]] TerrainGpuFieldData3D prepareTerrainGpuFieldData3D(
    const HeightField &field,
    const TerrainGpuMaterialResolver3D &resolveMaterial,
    std::uint32_t maxTextureSize);

} // namespace render
} // namespace demi::runtime
