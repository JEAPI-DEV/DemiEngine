#include "demi/runtime/scene/components/2dcomponents/BoxCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Tilemap2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/tilemap/TilemapAsset.h"
#include "demi/runtime/tilemap/TilemapCollisionGenerator.h"

#include <filesystem>
#include <fstream>
#include <iostream>

using namespace demi;
using namespace demi::runtime;

int main() {
  const auto path =
      std::filesystem::temp_directory_path() / "demi_tilemap_test.json";
  {
    std::ofstream output(path);
    output << R"({
      "format_version": 1,
      "texture": "asset://tiles/test",
      "tile_size": [16, 8],
      "map_size": [4, 2],
      "layers": [
        {"name": "ground", "collision": true,
         "collision_layer": "platform",
         "tiles": [0, 0, 0, 0, 1, 1, 0, 1]},
        {"name": "clouds", "parallax": 0.5,
         "tiles": [0, 2, 0, 0, 0, 0, 0, 0]}
      ]
    })";
  }
  const AssetManifest manifest{
      .id = "asset://maps/test", .type = "Tilemap2D", .sourcePath = path};
  std::string error;
  const auto asset = loadTilemapAsset(manifest, error);
  if (!asset || asset->columns != 4 || asset->layers.size() != 2 ||
      asset->layers[1].parallax != 0.5F) {
    std::cerr << "Tilemap parsing failed: " << error << '\n';
    return 1;
  }

  Entity map;
  map.id = "map";
  Transform2DComponent transform;
  transform.position = {-2.0F, -1.0F};
  map.setComponent(std::move(transform));
  Tilemap2DComponent tilemap;
  tilemap.asset = manifest.id;
  tilemap.pixelsPerUnit = 8.0F;
  map.setComponent(std::move(tilemap));
  World world;
  world.entities.push_back(std::move(map));
  AssetRegistry registry;
  registry.assets.push_back(manifest);
  generateTilemapColliders(world, registry);

  if (world.entities.size() != 3) {
    std::cerr << "Tilemap collision runs were not merged deterministically.\n";
    return 1;
  }
  const auto *first = world.entities[1].component<BoxCollider2DComponent>();
  const auto *firstTransform =
      world.entities[1].component<Transform2DComponent>();
  if (first == nullptr || firstTransform == nullptr || first->size.x != 4.0F ||
      first->size.y != 1.0F || first->layer != "platform" ||
      first->debugVisible || firstTransform->position.x != 0.0F ||
      firstTransform->position.y != -0.5F) {
    std::cerr << "Generated tilemap collider geometry is incorrect.\n";
    return 1;
  }
  std::filesystem::remove(path);
  return 0;
}
