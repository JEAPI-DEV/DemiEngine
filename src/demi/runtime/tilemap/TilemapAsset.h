#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

struct TilemapLayer2D {
  std::string name;
  std::vector<int> tiles;
  float parallax = 1.0F;
  float opacity = 1.0F;
  bool collision = false;
  std::string collisionLayer;
  bool navigationBlocked = false;
  float navigationCost = 1.0F;
};

struct TilemapTileset2D {
  std::string texture;
  int firstTile = 1;
  int tileWidth = 1;
  int tileHeight = 1;
};

struct AnimatedTileFrame2D {
  int tile = 0;
  float duration = 0.1F;
};

struct TilemapObject2D {
  std::string id;
  std::string type;
  Vec2 position;
  Vec2 size;
  nlohmann::json properties;
};

struct TilemapObjectLayer2D {
  std::string name;
  std::vector<TilemapObject2D> objects;
};

struct TilemapAsset2D {
  std::string texture;
  int tileWidth = 1;
  int tileHeight = 1;
  int columns = 0;
  int rows = 0;
  std::vector<TilemapLayer2D> layers;
  std::vector<TilemapTileset2D> tilesets;
  std::unordered_map<int, std::vector<AnimatedTileFrame2D>> animations;
  std::vector<TilemapObjectLayer2D> objectLayers;
};

[[nodiscard]] std::optional<TilemapAsset2D>
loadTilemapAsset(const AssetManifest &manifest, std::string &error);

} // namespace demi::runtime
