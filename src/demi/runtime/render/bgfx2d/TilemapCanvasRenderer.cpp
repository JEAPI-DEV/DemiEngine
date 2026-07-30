#include "demi/runtime/render/bgfx2d/TilemapCanvasRenderer.h"

#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/Tilemap2DComponent.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime::render {

TilemapCanvasRenderer::TilemapCanvasRenderer(
    Canvas2D &canvas, const TextureLibrary2D &textures,
    const std::unordered_map<std::string, TilemapAsset2D> &tilemaps)
    : canvas_(canvas), textures_(textures), tilemaps_(tilemaps) {}

bool TilemapCanvasRenderer::draw(const World &world,
                                 const Camera2DComponent &camera,
                                 const Vec2 cameraPosition,
                                 const std::uint16_t viewportWidth,
                                 const std::uint16_t viewportHeight,
                                 const float animationTime) {
  const float screenPpu = viewportHeight /
                          std::max(camera.orthographicSize * 2.0F, 1.0F);
  for (const Entity &entity : world.entities) {
    const auto *component = entity.component<Tilemap2DComponent>();
    if (!entity.enabled || component == nullptr ||
        component->pixelsPerUnit <= 0.0F)
      continue;
    const auto assetEntry = tilemaps_.find(component->asset);
    if (assetEntry == tilemaps_.end())
      continue;
    const TilemapAsset2D &asset = assetEntry->second;
    const Vec2 origin = worldPosition2D(world, entity);
    const float tileWorldWidth = asset.tileWidth / component->pixelsPerUnit;
    const float tileWorldHeight = asset.tileHeight / component->pixelsPerUnit;
    for (const TilemapLayer2D &layer : asset.layers) {
      const Vec2 layerCamera{cameraPosition.x * layer.parallax,
                             cameraPosition.y * layer.parallax};
      const std::uint8_t alpha = static_cast<std::uint8_t>(
          std::lround(std::clamp(layer.opacity, 0.0F, 1.0F) * 255.0F));
      for (int row = 0; row < asset.rows; ++row) {
        for (int column = 0; column < asset.columns; ++column) {
          const std::size_t tileIndex =
              static_cast<std::size_t>(row * asset.columns + column);
          if (tileIndex >= layer.tiles.size())
            continue;
          const std::string overrideKey = layer.name + "/" +
                                          std::to_string(column) + "/" +
                                          std::to_string(row);
          const auto overridden = component->tileOverrides.find(overrideKey);
          int tile = overridden != component->tileOverrides.end()
                         ? overridden->second
                         : layer.tiles[tileIndex];
          if (tile <= 0)
            continue;
          if (const auto animation = asset.animations.find(tile);
              animation != asset.animations.end() &&
              !animation->second.empty()) {
            float duration = 0.0F;
            for (const AnimatedTileFrame2D &frame : animation->second)
              duration += frame.duration;
            float time =
                std::fmod(std::max(animationTime, 0.0F),
                          std::max(duration, 0.001F));
            for (const AnimatedTileFrame2D &frame : animation->second) {
              tile = frame.tile;
              time -= frame.duration;
              if (time <= 0.0F)
                break;
            }
          }
          const TilemapTileset2D *tileset = nullptr;
          for (const TilemapTileset2D &candidate : asset.tilesets) {
            if (candidate.firstTile > tile)
              break;
            tileset = &candidate;
          }
          if (tileset == nullptr || tileset->tileWidth <= 0 ||
              tileset->tileHeight <= 0)
            continue;
          const TextureView2D texture = textures_.find(tileset->texture);
          if (!texture.handle)
            continue;
          const int atlasColumns =
              std::max(texture.width / tileset->tileWidth, 1);
          const int atlasIndex = tile - tileset->firstTile;
          if (atlasIndex < 0)
            continue;
          const float centerX =
              viewportWidth * 0.5F +
              (origin.x + (column + 0.5F) * tileWorldWidth - layerCamera.x) *
                  screenPpu;
          const float centerY =
              viewportHeight * 0.5F -
              (origin.y + (asset.rows - row - 0.5F) * tileWorldHeight -
               layerCamera.y) *
                  screenPpu;
          const float width = tileWorldWidth * screenPpu;
          const float height = tileWorldHeight * screenPpu;
          if (centerX + width * 0.5F < 0.0F ||
              centerX - width * 0.5F > viewportWidth ||
              centerY + height * 0.5F < 0.0F ||
              centerY - height * 0.5F > viewportHeight)
            continue;
          const float sourceX =
              (atlasIndex % atlasColumns) * tileset->tileWidth;
          const float sourceY =
              (atlasIndex / atlasColumns) * tileset->tileHeight;
          if (!canvas_.image(
                  texture.handle,
                  {.x = centerX - width * 0.5F,
                   .y = centerY - height * 0.5F,
                   .width = width,
                   .height = height},
                  {.u0 = sourceX / texture.width,
                   .v0 = sourceY / texture.height,
                   .u1 = (sourceX + tileset->tileWidth) / texture.width,
                   .v1 = (sourceY + tileset->tileHeight) / texture.height},
                  0x00ffffffU | (static_cast<std::uint32_t>(alpha) << 24U)))
            return false;
        }
      }
    }
  }
  return true;
}

} // namespace demi::runtime::render
