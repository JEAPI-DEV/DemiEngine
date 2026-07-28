#include "demi/runtime/navigation/NavigationGrid2D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/BoxCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Tilemap2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/tilemap/TilemapAsset.h"
#include "demi/runtime/tilemap/TilemapCollisionGenerator.h"
#include "demi/runtime/tilemap/TilemapRuntime.h"

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
      "tile_size": [16, 16],
      "map_size": [4, 2],
      "tilesets": [
        {"texture": "asset://tiles/test", "first_tile": 1},
        {"texture": "asset://tiles/props", "first_tile": 10,
         "tile_width": 32, "tile_height": 32}
      ],
      "animations": {
        "2": [{"tile": 2, "duration": 0.1},
              {"tile": 3, "duration": 0.2}]
      },
      "object_layers": [{
        "name": "spawns",
        "objects": [{"id": "spawn-a", "type": "spawn",
                     "position": [24, 16], "size": [16, 16]}]
      }],
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
      asset->layers[1].parallax != 0.5F || asset->tilesets.size() != 2 ||
      asset->animations.at(2).size() != 2 ||
      asset->objectLayers.front().objects.front().id != "spawn-a") {
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
      first->size.y != 2.0F || first->layer != "platform" ||
      first->debugVisible || firstTransform->position.x != 0.0F ||
      firstTransform->position.y != 0.0F) {
    std::cerr << "Generated tilemap collider geometry is incorrect.\n";
    return 1;
  }
  TilemapRuntime runtime;
  navigation::NavigationGrid2D navigation;
  runtime.attach(&world, &registry, &navigation);
  if (runtime.objects("map", "spawns").size() != 1) {
    std::cerr << "Tilemap object layer query failed.\n";
    return 1;
  }
  if (!runtime.bakeNavigation("map") || navigation.blocked({2, 0})) {
    std::cerr << "Tilemap navigation metadata did not bake.\n";
    return 1;
  }
  if (runtime.tile("map", "ground", 2, 1) != 0 ||
      !runtime.setTile("map", "ground", 2, 1, 1) ||
      runtime.tile("map", "ground", 2, 1) != 1 ||
      !world.tilemapCollisionDirty || !navigation.blocked({2, 0}) ||
      findEntity(world, "map")
          ->component<Tilemap2DComponent>()
          ->dirtyChunks.empty()) {
    std::cerr << "Runtime tile edit did not dirty collision/navigation.\n";
    return 1;
  }
  generateTilemapColliders(world, registry);
  if (world.tilemapCollisionDirty || world.entities.size() != 2 ||
      world.entities[1].component<BoxCollider2DComponent>()->size.x != 8.0F ||
      !findEntity(world, "map")
           ->component<Tilemap2DComponent>()
           ->dirtyChunks.empty()) {
    std::cerr << "Runtime tile edit did not rebuild merged collision.\n";
    return 1;
  }
  if (!runtime.clearOverrides("map") || !world.tilemapCollisionDirty ||
      runtime.tile("map", "ground", 2, 1) != 0 || navigation.blocked({2, 0})) {
    std::cerr << "Runtime tile override reset failed.\n";
    return 1;
  }
  std::filesystem::remove(path);
  return 0;
}
