#pragma once

#include "demi/assets/AssetRegistry.h"

#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {

struct TilemapLayer2D {
  std::string name;
  std::vector<int> tiles;
  float parallax = 1.0F;
  float opacity = 1.0F;
  bool collision = false;
  std::string collisionLayer;
};

struct TilemapAsset2D {
  std::string texture;
  int tileWidth = 1;
  int tileHeight = 1;
  int columns = 0;
  int rows = 0;
  std::vector<TilemapLayer2D> layers;
};

[[nodiscard]] std::optional<TilemapAsset2D>
loadTilemapAsset(const AssetManifest &manifest, std::string &error);

} // namespace demi::runtime
