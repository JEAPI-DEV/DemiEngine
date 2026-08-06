#include "demi/runtime/tilemap/TilemapAsset.h"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace demi::runtime {

std::optional<TilemapAsset2D> loadTilemapAsset(const AssetManifest &manifest,
                                               std::string &error) {
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
    if (!asset.texture.empty()) {
      asset.tilesets.push_back({.texture = asset.texture,
                                .firstTile = 1,
                                .tileWidth = asset.tileWidth,
                                .tileHeight = asset.tileHeight});
    }
    if (json.contains("tilesets") && json["tilesets"].is_array()) {
      asset.tilesets.clear();
      for (const auto &tilesetJson : json["tilesets"]) {
        TilemapTileset2D tileset;
        tileset.texture = tilesetJson.value("texture", std::string{});
        tileset.firstTile = std::max(tilesetJson.value("first_tile", 1), 1);
        tileset.tileWidth =
            std::max(tilesetJson.value("tile_width", asset.tileWidth), 1);
        tileset.tileHeight =
            std::max(tilesetJson.value("tile_height", asset.tileHeight), 1);
        if (!tileset.texture.empty())
          asset.tilesets.push_back(std::move(tileset));
      }
      std::ranges::sort(asset.tilesets, {}, &TilemapTileset2D::firstTile);
      if (!asset.tilesets.empty())
        asset.texture = asset.tilesets.front().texture;
    }
    if (asset.tilesets.empty() || asset.columns == 0 || asset.rows == 0 ||
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
      layer.opacity = std::clamp(layerJson.value("opacity", 1.0F), 0.0F, 1.0F);
      layer.collision = layerJson.value("collision", false);
      layer.collisionLayer =
          layerJson.value("collision_layer", std::string{"world"});
      layer.navigationBlocked =
          layerJson.value("navigation_blocked", layer.collision);
      layer.navigationCost =
          std::max(layerJson.value("navigation_cost", 1.0F), 1.0F);
      layer.tiles = layerJson.value("tiles", std::vector<int>{});
      if (layer.name.empty() || layer.tiles.size() != cellCount) {
        error = "every tilemap layer needs a name and exactly map_size tiles";
        return std::nullopt;
      }
      asset.layers.push_back(std::move(layer));
    }
    if (json.contains("animations") && json["animations"].is_object()) {
      for (auto iterator = json["animations"].begin();
           iterator != json["animations"].end(); ++iterator) {
        const int sourceTile = std::stoi(iterator.key());
        for (const auto &frameJson : iterator.value()) {
          asset.animations[sourceTile].push_back(
              {.tile = frameJson.value("tile", sourceTile),
               .duration =
                   std::max(frameJson.value("duration", 0.1F), 0.001F)});
        }
      }
    }
    if (json.contains("object_layers") && json["object_layers"].is_array()) {
      for (const auto &layerJson : json["object_layers"]) {
        TilemapObjectLayer2D objectLayer;
        objectLayer.name = layerJson.value("name", std::string{});
        for (const auto &objectJson :
             layerJson.value("objects", nlohmann::json::array())) {
          TilemapObject2D object;
          object.id = objectJson.value("id", std::string{});
          object.type = objectJson.value("type", std::string{});
          if (objectJson.contains("position") &&
              objectJson["position"].is_array() &&
              objectJson["position"].size() == 2)
            object.position = {objectJson["position"][0].get<float>(),
                               objectJson["position"][1].get<float>()};
          if (objectJson.contains("size") && objectJson["size"].is_array() &&
              objectJson["size"].size() == 2)
            object.size = {objectJson["size"][0].get<float>(),
                           objectJson["size"][1].get<float>()};
          object.properties =
              objectJson.value("properties", nlohmann::json::object());
          objectLayer.objects.push_back(std::move(object));
        }
        if (!objectLayer.name.empty())
          asset.objectLayers.push_back(std::move(objectLayer));
      }
    }
    return asset;
  } catch (const std::exception &exception) {
    error = "invalid tilemap JSON: " + std::string(exception.what());
    return std::nullopt;
  }
}

} // namespace demi::runtime
