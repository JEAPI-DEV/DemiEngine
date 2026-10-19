#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/MaterialSurface.h"
#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/ImageDecoder2D.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime::render {

struct TerrainMaterialImageSource3D {
  std::shared_ptr<const std::vector<std::byte>> encoded;
  // Verified against the decoded image before allocating packed arrays.
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  std::size_t encodedBytes = 0;
  std::size_t decoderWorkingBytes = 0;
};

struct TerrainMaterialLayerSource3D {
  std::string materialId;
  assets::MaterialSurface surface;
  TextureWrap wrap = TextureWrap::Repeat;
  std::map<assets::MaterialMapSlot, TerrainMaterialImageSource3D> images;
};

struct TerrainMaterialArraySource3D {
  // Covers ordered materials, their parameters, image content and sampling.
  // A changed source must receive a changed identity.
  std::string identity;
  std::vector<TerrainMaterialLayerSource3D> layers;
  [[nodiscard]] std::optional<std::size_t>
  layerIndex(std::string_view materialId) const;
};

// Empty ID always occupies a neutral layer. Material IDs are deduplicated and
// sorted. Encoded bytes are snapshotted once per texture ID.
[[nodiscard]] std::shared_ptr<const TerrainMaterialArraySource3D>
createTerrainMaterialArraySource3D(const AssetRegistry &registry,
                                   const std::vector<std::string> &materialIds,
                                   std::string &error);

enum class TerrainMaterialArraySlot3D {
  Albedo,
  Normal,
  OcclusionRoughnessMetallicHeight,
  DetailNormal,
  Emissive,
  Count
};
inline constexpr std::size_t terrainMaterialArrayCount3D =
    static_cast<std::size_t>(TerrainMaterialArraySlot3D::Count);

struct TerrainMaterialArrayData3D {
  std::uint16_t edge = 0;
  // At least two physical layers ensure a native array view for one material.
  std::uint16_t physicalLayers = 0;
  // Globally absent maps keep one texel per layer instead of a full neutral
  // mip chain. edge remains the selected resolution for all real maps.
  std::array<std::uint16_t, terrainMaterialArrayCount3D> arrayEdges{};
  std::size_t logicalLayers = 0;
  std::array<std::vector<std::byte>, terrainMaterialArrayCount3D> pixels;
  [[nodiscard]] std::size_t bytes() const;
};

// Five RGBA8 arrays, each layer containing its full mip chain. Color arrays
// remain sRGB encoded; resizing/mip filtering operates in linear light.
[[nodiscard]] std::optional<TerrainMaterialArrayData3D>
prepareTerrainMaterialArrayData3D(const TerrainMaterialArraySource3D &source,
                                  std::uint16_t maximumEdge, std::string &error,
                                  std::stop_token stop = {});

// Exact uncompressed GPU payload for the selected edge and physical layers.
[[nodiscard]] std::size_t
terrainMaterialArrayBytes3D(const TerrainMaterialArraySource3D &source,
                            std::uint16_t maximumEdge);

// Conservative working reservation: encoded inputs, decoded maps, resized
// layer/mip scratch and all packed output. Not a hard driver/RSS limit.
[[nodiscard]] std::size_t
terrainMaterialArrayReservation3D(const TerrainMaterialArraySource3D &source,
                                  std::uint16_t maximumEdge);

} // namespace demi::runtime::render
