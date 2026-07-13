#include "demi/runtime/tilemap/TilemapCollisionGenerator.h"

#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/BoxCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Tilemap2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/tilemap/TilemapAsset.h"

#include <algorithm>

namespace demi::runtime {

namespace {
constexpr std::string_view GeneratedPrefix = "__tilemap_collider/";
}

void generateTilemapColliders(World &world, const AssetRegistry &registry) {
  std::erase_if(world.entities, [](const Entity &entity) {
    return entity.id.starts_with(GeneratedPrefix);
  });

  std::vector<Entity> colliders;
  for (const Entity &entity : world.entities) {
    const auto *tilemap = entity.component<Tilemap2DComponent>();
    if (tilemap == nullptr)
      continue;
    const AssetManifest *manifest = findAsset(registry, tilemap->asset);
    if (manifest == nullptr)
      continue;
    std::string error;
    const auto asset = loadTilemapAsset(*manifest, error);
    if (!asset)
      continue;

    const Vec2 origin = worldPosition2D(world, entity);
    const float cellWidth =
        static_cast<float>(asset->tileWidth) / tilemap->pixelsPerUnit;
    const float cellHeight =
        static_cast<float>(asset->tileHeight) / tilemap->pixelsPerUnit;
    for (const TilemapLayer2D &layer : asset->layers) {
      if (!layer.collision)
        continue;
      for (int row = 0; row < asset->rows; ++row) {
        int column = 0;
        while (column < asset->columns) {
          const auto tileAt = [&](const int x) {
            return layer.tiles[static_cast<std::size_t>(
                       row * asset->columns + x)] > 0;
          };
          if (!tileAt(column)) {
            ++column;
            continue;
          }
          const int start = column;
          while (column < asset->columns && tileAt(column))
            ++column;
          const int count = column - start;
          Entity collider;
          collider.id = std::string(GeneratedPrefix) + entity.id + "/" +
                        layer.name + "/" + std::to_string(row) + "/" +
                        std::to_string(start);
          collider.name = "Generated tilemap collider";
          Transform2DComponent transform;
          transform.position = {
              origin.x +
                  (static_cast<float>(start) + count * 0.5F) * cellWidth,
              origin.y + (static_cast<float>(asset->rows - row) - 0.5F) *
                             cellHeight};
          collider.setComponent(std::move(transform));
          BoxCollider2DComponent box;
          box.size = {count * cellWidth, cellHeight};
          box.layer = layer.collisionLayer;
          box.debugVisible = false;
          collider.setComponent(std::move(box));
          colliders.push_back(std::move(collider));
        }
      }
    }
  }
  world.entities.insert(world.entities.end(),
                        std::make_move_iterator(colliders.begin()),
                        std::make_move_iterator(colliders.end()));
}

} // namespace demi::runtime
