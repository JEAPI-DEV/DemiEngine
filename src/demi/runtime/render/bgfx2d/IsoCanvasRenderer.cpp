#include "demi/runtime/render/bgfx2d/IsoCanvasRenderer.h"

#include "demi/runtime/isometric/IsoGridMath.h"
#include "demi/runtime/isometric/IsoWorldQueries.h"
#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/IsoGridComponent.h"
#include "demi/runtime/scene/components/2dcomponents/IsoTransformComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/gameplay/BuildableComponent.h"

#include <algorithm>
#include <ranges>
#include <tuple>
#include <vector>

namespace demi::runtime::render {
namespace {

struct IsoProjection {
  float ppu;
  float centerX;
  float centerY;
  Vec2 camera;

  [[nodiscard]] Vec2 point(const Vec2 world) const {
    return {centerX + (world.x - camera.x) * ppu,
            centerY - (world.y - camera.y) * ppu};
  }
};

} // namespace

IsoCanvasRenderer::IsoCanvasRenderer(Canvas2D &canvas,
                                     const TextureLibrary2D &textures)
    : canvas_(canvas), textures_(textures) {}

bool IsoCanvasRenderer::draw(const World &world,
                             const Camera2DComponent &camera,
                             const Vec2 cameraPosition,
                             const std::uint16_t viewportWidth,
                             const std::uint16_t viewportHeight) {
  const auto gridDefinition = isometric::gridDefinition(world);
  if (!gridDefinition)
    return true;
  const isometric::GridDefinition &grid = *gridDefinition;
  const IsoProjection projection{
      .ppu = viewportHeight / std::max(camera.orthographicSize * 2.0F, 1.0F),
      .centerX = viewportWidth * 0.5F,
      .centerY = viewportHeight * 0.5F,
      .camera = cameraPosition,
  };

  for (const Entity &entity : world.entities) {
    const auto *isoGrid = entity.component<IsoGridComponent>();
    if (!entity.enabled || isoGrid == nullptr)
      continue;
    for (int depth = 0; depth < isoGrid->width + isoGrid->height - 1; ++depth) {
      const int firstX = std::max(0, depth - isoGrid->height + 1);
      const int lastX = std::min(isoGrid->width - 1, depth);
      for (int x = firstX; x <= lastX; ++x) {
        const int y = depth - x;
        const Vec2 worldPosition = isometric::tileToWorld(
            grid, Vec2{static_cast<float>(x), static_cast<float>(y)});
        const std::string key = std::to_string(x) + "," + std::to_string(y);
        const auto authored = isoGrid->cellTextures.find(key);
        const std::string &textureId = authored != isoGrid->cellTextures.end()
                                           ? authored->second
                                           : isoGrid->defaultTexture;
        const TextureView2D texture = textures_.find(textureId);
        if (texture.handle) {
          const Vec2 center = projection.point(worldPosition);
          const float halfWidth = projection.ppu * grid.cellWidth;
          const float halfHeight = projection.ppu * grid.cellHeight;
          const float width = halfWidth * 2.0F;
          const float height =
              width * texture.height / std::max<float>(texture.width, 1.0F);
          if (!canvas_.image(texture.handle, {.x = center.x - halfWidth,
                                              .y = center.y - halfHeight,
                                              .width = width,
                                              .height = height}))
            return false;
        }
        const Vec2 center = projection.point(worldPosition);
        const float halfWidth = projection.ppu * grid.cellWidth;
        const float halfHeight = projection.ppu * grid.cellHeight;
        const Vec2 top{center.x, center.y - halfHeight};
        const Vec2 right{center.x + halfWidth, center.y};
        const Vec2 bottom{center.x, center.y + halfHeight};
        const Vec2 left{center.x - halfWidth, center.y};
        if (!canvas_.line(top.x, top.y, right.x, right.y, 1.0F, 0xbe5c7046U) ||
            !canvas_.line(right.x, right.y, bottom.x, bottom.y, 1.0F,
                          0xbe5c7046U) ||
            !canvas_.line(bottom.x, bottom.y, left.x, left.y, 1.0F,
                          0xbe5c7046U) ||
            !canvas_.line(left.x, left.y, top.x, top.y, 1.0F, 0xbe5c7046U))
          return false;
      }
    }
  }

  std::vector<const Entity *> entities;
  for (const Entity &entity : world.entities)
    if (entity.enabled && entity.hasComponent<IsoTransformComponent>() &&
        !entity.hasComponent<IsoGridComponent>())
      entities.push_back(&entity);
  std::ranges::stable_sort(entities, [&world](const Entity *left,
                                              const Entity *right) {
    const IsoTransformComponent a = worldIsoTransform(world, *left);
    const IsoTransformComponent b = worldIsoTransform(world, *right);
    const auto key = [](const Entity *entity,
                        const IsoTransformComponent &transform) {
      const auto *sprite = entity->component<SpriteComponent>();
      return std::tuple{sprite != nullptr ? sprite->sortingOrder : 0,
                        sprite != nullptr ? sprite->layer : std::string{},
                        transform.tile.x + transform.tile.y + transform.height,
                        entity->id};
    };
    return key(left, a) < key(right, b);
  });

  for (const Entity *entity : entities) {
    const IsoTransformComponent transform = worldIsoTransform(world, *entity);
    Vec2 renderTile = transform.tile;
    if (entity->hasComponent<BuildableComponent>()) {
      renderTile.x += (transform.footprint.x - 1.0F) * 0.5F;
      renderTile.y += (transform.footprint.y - 1.0F) * 0.5F;
    }
    Vec2 worldPosition = isometric::tileToWorld(grid, renderTile);
    worldPosition.y += transform.height;
    const Vec2 screen = projection.point(worldPosition);
    const auto *sprite = entity->component<SpriteComponent>();
    const auto *buildable = entity->component<BuildableComponent>();
    const std::string textureId = sprite != nullptr      ? sprite->texture
                                  : buildable != nullptr ? buildable->asset
                                                         : "";
    const TextureView2D texture = textures_.find(textureId);
    const float footprintScale =
        std::max(transform.footprint.x + transform.footprint.y, 1.0F);
    const float width = sprite != nullptr && sprite->size.x > 0.0F
                            ? sprite->size.x * projection.ppu
                            : footprintScale * projection.ppu * 0.62F;
    const float height =
        sprite != nullptr && sprite->size.y > 0.0F
            ? sprite->size.y * projection.ppu
            : footprintScale * projection.ppu * grid.cellHeight * 0.95F;
    const float bottomOffset =
        footprintScale * grid.cellHeight * projection.ppu * 0.5F;
    const float pivotX = sprite != nullptr ? sprite->pivot.x : 0.5F;
    const float pivotY = sprite != nullptr ? sprite->pivot.y : 1.0F;
    if (texture.handle) {
      if (!canvas_.imageTransformed(
              texture.handle, screen.x, screen.y + bottomOffset, width, height,
              pivotX, pivotY, 0.0F, {},
              sprite != nullptr ? packVertexColorRgba8(sprite->color)
                                : 0xffffffffU))
        return false;
    } else if (!canvas_.solid({.x = screen.x - width * 0.5F,
                               .y = screen.y + bottomOffset - height,
                               .width = width,
                               .height = height},
                              0xff52a6deU)) {
      return false;
    }
  }
  return true;
}

} // namespace demi::runtime::render
