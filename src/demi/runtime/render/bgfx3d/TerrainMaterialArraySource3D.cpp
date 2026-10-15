#include "demi/runtime/render/bgfx3d/TerrainMaterialArrayData3D.h"

#include "demi/assets/AssetHash.h"
#include "demi/runtime/render/backend/RenderAssetLoading.h"

#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include <set>

namespace demi::runtime::render {
namespace {

std::shared_ptr<const std::vector<std::byte>>
readSourceBytes(const std::filesystem::path &path, std::string &error) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  const auto size = stream ? stream.tellg() : std::streampos{-1};
  if (size <= 0 || static_cast<std::uint64_t>(size) > UINT32_MAX) {
    error =
        "Terrain source is empty, unavailable or exceeds the decoder limit: " +
        path.string();
    return {};
  }
  auto bytes =
      std::make_shared<std::vector<std::byte>>(static_cast<std::size_t>(size));
  stream.seekg(0);
  if (!stream.read(reinterpret_cast<char *>(bytes->data()),
                   static_cast<std::streamsize>(bytes->size()))) {
    error = "Could not read the complete terrain source: " + path.string();
    return {};
  }
  return bytes;
}

std::string hashImage(const std::vector<std::byte> &bytes) {
  return assets::hashBytes(
      {reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size()});
}

} // namespace

std::optional<std::size_t>
TerrainMaterialArraySource3D::layerIndex(std::string_view materialId) const {
  for (std::size_t index = 0; index < layers.size(); ++index) {
    if (layers[index].materialId == materialId) {
      return index;
    }
  }
  return std::nullopt;
}

std::shared_ptr<const TerrainMaterialArraySource3D>
createTerrainMaterialArraySource3D(const AssetRegistry &registry,
                                   const std::vector<std::string> &materialIds,
                                   std::string &error) {
  error.clear();
  auto source = std::make_shared<TerrainMaterialArraySource3D>();
  source->layers.emplace_back();
  std::set<std::string, std::less<>> orderedIds(materialIds.begin(),
                                                materialIds.end());
  orderedIds.erase("");
  std::map<std::string, TerrainMaterialImageSource3D, std::less<>> images;
  nlohmann::json identity = nlohmann::json::array();
  for (const auto &id : orderedIds) {
    const auto *asset = findAsset(registry, id);
    if (!asset || asset->type != "Material") {
      error = "Terrain layer requires an ordinary Material asset: " + id;
      return {};
    }
    const auto materialBytes = readSourceBytes(asset->sourcePath, error);
    if (!materialBytes) {
      return {};
    }
    const std::string materialText(
        reinterpret_cast<const char *>(materialBytes->data()),
        materialBytes->size());
    const auto document = nlohmann::json::parse(materialText, nullptr, false);
    if (document.is_discarded()) {
      error = "Terrain Material source is not valid JSON: " + id;
      return {};
    }
    Diagnostics diagnostics;
    const auto material =
        assets::parseMaterialAsset(document, asset->sourcePath, &diagnostics);
    if (!material) {
      error = diagnostics.empty() ? "Could not load terrain Material: " + id
                                  : diagnostics.front().message;
      return {};
    }
    const auto materialHash = hashImage(*materialBytes);
    TerrainMaterialLayerSource3D layer{
        .materialId = id, .surface = assets::materialSurface(*material)};
    std::optional<TextureWrap> wrap;
    nlohmann::json mapIdentity = nlohmann::json::array();
    for (const auto &[slot, textureId] : layer.surface.maps) {
      const auto *texture = findAsset(registry, textureId);
      if (!texture || texture->type != "Texture2D") {
        error = "Terrain map requires a Texture2D asset: " + textureId;
        return {};
      }
      const auto sampling = textureSampling2D(*texture);
      if (!sampling.mipmaps) {
        error = "Terrain material arrays require mipmapped sources; enable "
                "Mipmaps on Texture2D " +
                textureId + " or use the ordinary material path.";
        return {};
      }
      if (sampling.filter != TextureFilter::Linear) {
        error = "Terrain array layers require linear map filtering; this "
                "Material uses nearest filtering: " +
                id;
        return {};
      }
      if (wrap && *wrap != sampling.wrap) {
        error = "Terrain Material maps use different wrap modes; packed layers "
                "require one common wrap mode: " +
                id;
        return {};
      }
      wrap = sampling.wrap;
      auto found = images.find(textureId);
      if (found == images.end()) {
        const auto encoded = readSourceBytes(texture->sourcePath, error);
        ImageDimensions2D dimensions;
        if (!encoded || !inspectImage2D(*encoded, dimensions, error)) {
          return {};
        }
        found = images
                    .emplace(textureId,
                             TerrainMaterialImageSource3D{
                                 .encoded = encoded,
                                 .width = dimensions.width,
                                 .height = dimensions.height,
                                 .decoderWorkingBytes =
                                     dimensions.decoderWorkingBytes})
                    .first;
      }
      layer.images.emplace(slot, found->second);
      mapIdentity.push_back({assets::materialMapSlotName(slot), textureId,
                             hashImage(*found->second.encoded),
                             static_cast<int>(sampling.wrap)});
    }
    layer.wrap = wrap.value_or(TextureWrap::Repeat);
    identity.push_back({id, materialHash, std::move(mapIdentity)});
    source->layers.push_back(std::move(layer));
  }
  const auto serialized = identity.dump();
  source->identity = assets::hashBytes(
      {reinterpret_cast<const unsigned char *>(serialized.data()),
       serialized.size()});
  return source;
}

} // namespace demi::runtime::render
