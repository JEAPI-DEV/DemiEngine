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

using TerrainGpuMaterialResolver3D =
    std::function<const MaterialBinding *(std::string_view)>;

// CPU-only texture payloads. Unsupported content returns an empty result with a
// reason, so the ordinary material renderer remains the whole-field fallback.
struct TerrainGpuFieldData3D {
  bool compatible = false;
  std::string fallbackReason;
  DrawState state;
  std::uint16_t sampleWidth = 0;
  std::uint16_t sampleHeight = 0;
  std::uint16_t paletteWidth = 0;
  std::uint16_t paletteHeight = 0;
  std::size_t paletteEntries = 0;
  std::vector<float> heights;
  std::vector<std::array<float, 4>> normals;
  // An exact unsigned palette index in little-endian RGBA bytes.
  std::vector<std::array<std::uint8_t, 4>> materialIndices;
  // Two row-major RGBA32F texels per entry: tinted base RGBA, then
  // metallic/roughness/unlit/CW-cull flag. Tail texels are zero padding.
  std::vector<std::array<float, 4>> palette;
};

[[nodiscard]] TerrainGpuFieldData3D prepareTerrainGpuFieldData3D(
    const HeightField &field,
    const TerrainGpuMaterialResolver3D &resolveMaterial,
    std::uint32_t maxTextureSize);

} // namespace render
} // namespace demi::runtime
