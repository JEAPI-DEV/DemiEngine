#include "demi/runtime/tilemap/TilemapAsset.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace demi::runtime {

std::optional<TilemapAsset2D>
loadTilemapAsset(const AssetManifest &manifest, std::string &error) {
  if (manifest.type != "Tilemap2D") {
    error = "asset is not a Tilemap2D";
    return std::nullopt;
  }
  std::ifstream input(manifest.sourcePath);
  if (!input) {
    error = "could not open tilemap source " + manifest.sourcePath.string();
    return std::nullopt;
  }
  try {
    const nlohmann::json json = nlohmann::json::parse(input);
    TilemapAsset2D asset;
    asset.texture = json.value("texture", std::string{});
    if (json.contains("tile_size") && json["tile_size"].is_array() &&
        json["tile_size"].size() == 2) {
      asset.tileWidth = std::max(json["tile_size"][0].get<int>(), 1);
      asset.tileHeight = std::max(json["tile_size"][1].get<int>(), 1);
    }
    if (json.contains("map_size") && json["map_size"].is_array() &&
        json["map_size"].size() == 2) {
      asset.columns = std::max(json["map_size"][0].get<int>(), 0);
      asset.rows = std::max(json["map_size"][1].get<int>(), 0);
    }
    if (asset.texture.empty() || asset.columns == 0 || asset.rows == 0 ||
        !json.contains("layers") || !json["layers"].is_array()) {
      error = "tilemap requires texture, tile_size, map_size, and layers";
      return std::nullopt;
    }
    const std::size_t cellCount =
        static_cast<std::size_t>(asset.columns * asset.rows);
    for (const nlohmann::json &layerJson : json["layers"]) {
      TilemapLayer2D layer;
      layer.name = layerJson.value("name", std::string{});
      layer.parallax = layerJson.value("parallax", 1.0F);
      layer.opacity =
          std::clamp(layerJson.value("opacity", 1.0F), 0.0F, 1.0F);
      layer.collision = layerJson.value("collision", false);
      layer.collisionLayer =
          layerJson.value("collision_layer", std::string{"world"});
      layer.tiles = layerJson.value("tiles", std::vector<int>{});
      if (layer.name.empty() || layer.tiles.size() != cellCount) {
        error = "every tilemap layer needs a name and exactly map_size tiles";
        return std::nullopt;
      }
      asset.layers.push_back(std::move(layer));
    }
    return asset;
  } catch (const std::exception &exception) {
    error = "invalid tilemap JSON: " + std::string(exception.what());
    return std::nullopt;
  }
}

} // namespace demi::runtime
