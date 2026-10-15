#include "demi/runtime/render/bgfx3d/TerrainMaterialArrayData3D.h"
#include "demi/runtime/render/backend/ImageMipmaps2D.h"
#include "demi/runtime/render/backend/ImageResize2D.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace demi::runtime::render {
namespace {
using MapSlot = assets::MaterialMapSlot;
using ArraySlot = TerrainMaterialArraySlot3D;

std::size_t saturatedAdd(std::size_t first, std::size_t second) {
  return second > SIZE_MAX - first ? SIZE_MAX : first + second;
}

std::size_t saturatedMultiply(std::size_t first, std::size_t second) {
  return first != 0 && second > SIZE_MAX / first ? SIZE_MAX : first * second;
}

std::uint16_t targetEdge(const TerrainMaterialArraySource3D &source,
                         std::uint16_t maximumEdge) {
  unsigned edge = 1;
  for (const auto &layer : source.layers) {
    for (const auto &[slot, image] : layer.images) {
      edge = std::max({edge, unsigned(image.width), unsigned(image.height)});
    }
  }
  return static_cast<std::uint16_t>(std::min(edge, unsigned(maximumEdge)));
}

std::size_t arrayIndex(MapSlot slot) {
  switch (slot) {
  case MapSlot::Albedo:
    return static_cast<std::size_t>(ArraySlot::Albedo);
  case MapSlot::Normal:
    return static_cast<std::size_t>(ArraySlot::Normal);
  case MapSlot::AmbientOcclusion:
  case MapSlot::Roughness:
  case MapSlot::Metallic:
  case MapSlot::Height:
    return static_cast<std::size_t>(
        ArraySlot::OcclusionRoughnessMetallicHeight);
  case MapSlot::DetailNormal:
    return static_cast<std::size_t>(ArraySlot::DetailNormal);
  case MapSlot::Emissive:
    return static_cast<std::size_t>(ArraySlot::Emissive);
  }
  throw std::invalid_argument("Unknown terrain Material map slot");
}

constexpr std::array mapSlots{MapSlot::Albedo,           MapSlot::Normal,
                              MapSlot::Roughness,        MapSlot::Metallic,
                              MapSlot::AmbientOcclusion, MapSlot::Height,
                              MapSlot::DetailNormal,     MapSlot::Emissive};

std::array<std::uint16_t, terrainMaterialArrayCount3D>
arrayEdges(const TerrainMaterialArraySource3D &source,
           std::uint16_t maximumEdge) {
  std::array<std::uint16_t, terrainMaterialArrayCount3D> edges;
  edges.fill(1);
  const auto selectedEdge = targetEdge(source, maximumEdge);
  for (const auto &layer : source.layers) {
    for (const auto slot : mapSlots) {
      if (layer.images.contains(slot) || layer.surface.maps.contains(slot)) {
        edges[arrayIndex(slot)] = selectedEdge;
      }
    }
  }
  return edges;
}

std::size_t mipBytes(unsigned edge) {
  std::size_t bytes = 0;
  while (true) {
    bytes = saturatedAdd(bytes, saturatedMultiply(std::size_t(edge) * edge, 4));
    if (edge == 1) {
      return bytes;
    }
    edge = std::max(1U, edge / 2);
  }
}

ImageData2D neutralImage(unsigned edge, std::array<std::byte, 4> pixel) {
  ImageData2D image{.width = static_cast<std::uint16_t>(edge),
                    .height = static_cast<std::uint16_t>(edge)};
  image.rgba.resize(std::size_t(edge) * edge * 4);
  for (std::size_t offset = 0; offset < image.rgba.size(); offset += 4) {
    std::copy(pixel.begin(), pixel.end(), image.rgba.begin() + offset);
  }
  return image;
}

ImageMipSemantic mapSemantic(MapSlot slot) {
  if (slot == MapSlot::Albedo || slot == MapSlot::Emissive) {
    return ImageMipSemantic::Color;
  }
  if (slot == MapSlot::Normal || slot == MapSlot::DetailNormal) {
    return ImageMipSemantic::Normal;
  }
  return ImageMipSemantic::Linear;
}

std::array<std::byte, 4> neutralPixel(MapSlot slot,
                                      const assets::MaterialSurface &surface) {
  if (slot == MapSlot::Normal || slot == MapSlot::DetailNormal) {
    return {std::byte{128}, std::byte{128}, std::byte{255}, std::byte{255}};
  }
  if (slot == MapSlot::Emissive) {
    return {std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}};
  }
  if (slot == MapSlot::Roughness || slot == MapSlot::Metallic) {
    const float scalar =
        slot == MapSlot::Roughness ? surface.roughness : surface.metallic;
    const auto value = static_cast<std::byte>(std::lround(scalar * 255.0F));
    return {value, value, value, std::byte{255}};
  }
  if (slot == MapSlot::Height) {
    return {std::byte{128}, std::byte{128}, std::byte{128}, std::byte{255}};
  }
  return {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
}

void appendMipChain(std::vector<std::byte> &destination,
                    const ImageData2D &image, ImageMipSemantic semantic,
                    std::stop_token stop) {
  const auto chain = imageMipChain2D(image, semantic, stop);
  destination.insert(destination.end(), chain.begin(), chain.end());
}

} // namespace

std::size_t TerrainMaterialArrayData3D::bytes() const {
  std::size_t total = 0;
  for (const auto &array : pixels) {
    total = saturatedAdd(total, array.size());
  }
  return total;
}

std::size_t
terrainMaterialArrayBytes3D(const TerrainMaterialArraySource3D &source,
                            std::uint16_t maximumEdge) {
  if (maximumEdge == 0 || source.layers.empty()) {
    return SIZE_MAX;
  }
  const auto layers = std::max(std::size_t{2}, source.layers.size());
  std::size_t bytes = 0;
  for (const auto edge : arrayEdges(source, maximumEdge)) {
    bytes = saturatedAdd(bytes, saturatedMultiply(mipBytes(edge), layers));
  }
  return bytes;
}

std::size_t
terrainMaterialArrayReservation3D(const TerrainMaterialArraySource3D &source,
                                  std::uint16_t maximumEdge) {
  if (maximumEdge == 0) {
    return SIZE_MAX;
  }
  const auto edges = arrayEdges(source, maximumEdge);
  auto bytes = terrainMaterialArrayBytes3D(source, maximumEdge);
  std::size_t resizedMaps = 0;
  std::size_t largestResizedMap = 0;
  std::size_t largestMipChain = 0;
  for (const auto slot : mapSlots) {
    const auto edge = edges[arrayIndex(slot)];
    const auto baseBytes = saturatedMultiply(std::size_t(edge) * edge, 4);
    resizedMaps = saturatedAdd(resizedMaps, baseBytes);
    largestResizedMap = std::max(largestResizedMap, baseBytes);
    largestMipChain = std::max(largestMipChain, mipBytes(edge));
  }
  const auto ormBytes = saturatedMultiply(std::size_t(edges[2]) * edges[2], 4);
  // One layer's maps/ORM, a replacement resize buffer, and mip vector growth
  // plus returned-chain storage. Slot dimensions include compact neutral maps.
  auto scratch = saturatedAdd(resizedMaps, ormBytes);
  scratch = saturatedAdd(scratch, largestResizedMap);
  scratch = saturatedAdd(scratch, saturatedMultiply(largestMipChain, 3));
  bytes = saturatedAdd(bytes, scratch);
  std::size_t peakDecoderBytes = 0;
  std::set<const std::vector<std::byte> *> encodedSnapshots;
  std::set<std::string, std::less<>> metadataTextureIds;
  for (const auto &layer : source.layers) {
    for (const auto &[slot, image] : layer.images) {
      const auto decodedBytes =
          saturatedMultiply(std::size_t(image.width) * image.height, 4);
      const auto decoderBytes = image.decoderWorkingBytes != 0
                                    ? image.decoderWorkingBytes
                                    : saturatedMultiply(decodedBytes, 2);
      peakDecoderBytes = std::max(peakDecoderBytes, decoderBytes);
      bool uniqueEncodedInput = false;
      if (image.encoded) {
        uniqueEncodedInput =
            encodedSnapshots.insert(image.encoded.get()).second;
      } else {
        const auto asset = layer.surface.maps.find(slot);
        uniqueEncodedInput = asset == layer.surface.maps.end() ||
                             metadataTextureIds.insert(asset->second).second;
      }
      if (uniqueEncodedInput) {
        bytes = saturatedAdd(bytes, image.encoded ? image.encoded->size()
                                                  : image.encodedBytes);
      }
    }
  }
  // Maps are decoded sequentially. Only the largest decoder/conversion peak
  // overlaps the output and one-layer resized/mip working buffers.
  bytes = saturatedAdd(bytes, peakDecoderBytes);
  return bytes;
}

std::optional<TerrainMaterialArrayData3D>
prepareTerrainMaterialArrayData3D(const TerrainMaterialArraySource3D &source,
                                  std::uint16_t maximumEdge, std::string &error,
                                  std::stop_token stop) {
  error.clear();
  if (stop.stop_requested()) {
    return std::nullopt;
  }
  if (source.layers.empty() || source.layers.size() > UINT16_MAX ||
      maximumEdge == 0) {
    error = "Terrain material arrays require nonempty layers and a positive "
            "resolution.";
    return std::nullopt;
  }
  TerrainMaterialArrayData3D output;
  output.edge = targetEdge(source, maximumEdge);
  output.arrayEdges = arrayEdges(source, maximumEdge);
  output.logicalLayers = source.layers.size();
  output.physicalLayers = static_cast<std::uint16_t>(
      std::max(std::size_t{2}, source.layers.size()));
  for (std::size_t slot = 0; slot < output.pixels.size(); ++slot) {
    output.pixels[slot].reserve(mipBytes(output.arrayEdges[slot]) *
                                output.physicalLayers);
  }
  for (std::size_t index = 0; index < output.physicalLayers; ++index) {
    if (stop.stop_requested()) {
      return std::nullopt;
    }
    const auto &layer =
        source.layers[std::min(index, source.layers.size() - 1)];
    const bool validRoughness = std::isfinite(layer.surface.roughness) &&
                                layer.surface.roughness >= 0 &&
                                layer.surface.roughness <= 1;
    const bool validMetallic = std::isfinite(layer.surface.metallic) &&
                               layer.surface.metallic >= 0 &&
                               layer.surface.metallic <= 1;
    if (!validRoughness || !validMetallic) {
      error = "Terrain Material scalar roughness and metallic must be finite "
              "in [0,1]: " +
              layer.materialId;
      return std::nullopt;
    }
    std::map<MapSlot, ImageData2D> maps;
    for (const auto slot : mapSlots) {
      const auto edge = output.arrayEdges[arrayIndex(slot)];
      const auto found = layer.images.find(slot);
      auto image = neutralImage(edge, neutralPixel(slot, layer.surface));
      if (found != layer.images.end()) {
        const auto &input = found->second;
        ImageData2D decoded;
        if (!input.encoded || !decodeImage2D(*input.encoded, decoded, error)) {
          error = "Terrain map decode failed for " + layer.materialId + ": " +
                  error;
          return std::nullopt;
        }
        if (decoded.width != input.width || decoded.height != input.height) {
          error = "Terrain map dimensions changed after budget planning: " +
                  layer.materialId;
          return std::nullopt;
        }
        image = resizeImage2D(decoded, edge, edge, mapSemantic(slot), stop);
        if (stop.stop_requested()) {
          return std::nullopt;
        }
      } else if (layer.surface.maps.contains(slot)) {
        error = "Terrain Material map has no immutable image snapshot: " +
                layer.materialId;
        return std::nullopt;
      }
      maps.emplace(slot, std::move(image));
    }
    auto orm = neutralImage(output.arrayEdges[2], {});
    for (std::size_t offset = 0; offset < orm.rgba.size(); offset += 4) {
      if ((offset & 1023) == 0 && stop.stop_requested()) {
        return std::nullopt;
      }
      orm.rgba[offset] = maps.at(MapSlot::AmbientOcclusion).rgba[offset];
      orm.rgba[offset + 1] = maps.at(MapSlot::Roughness).rgba[offset];
      orm.rgba[offset + 2] = maps.at(MapSlot::Metallic).rgba[offset];
      orm.rgba[offset + 3] = maps.at(MapSlot::Height).rgba[offset];
    }
    appendMipChain(output.pixels[0], maps.at(MapSlot::Albedo),
                   ImageMipSemantic::Color, stop);
    appendMipChain(output.pixels[1], maps.at(MapSlot::Normal),
                   ImageMipSemantic::Normal, stop);
    appendMipChain(output.pixels[2], orm, ImageMipSemantic::Linear, stop);
    appendMipChain(output.pixels[3], maps.at(MapSlot::DetailNormal),
                   ImageMipSemantic::Normal, stop);
    appendMipChain(output.pixels[4], maps.at(MapSlot::Emissive),
                   ImageMipSemantic::Color, stop);
  }
  if (stop.stop_requested()) {
    return std::nullopt;
  }
  return output;
}

} // namespace demi::runtime::render
