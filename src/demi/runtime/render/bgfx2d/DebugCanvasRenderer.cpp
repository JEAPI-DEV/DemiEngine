#include "demi/runtime/render/bgfx2d/DebugCanvasRenderer.h"

#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/BoxCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/CircleCollider2DComponent.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace demi::runtime::render {
namespace {

struct Projection2D {
  float ppu;
  float centerX;
  float centerY;
  Vec2 camera;

  [[nodiscard]] Vec2 point(const Vec2 world) const {
    return {centerX + (world.x - camera.x) * ppu,
            centerY - (world.y - camera.y) * ppu};
  }
};

Projection2D projection(const Camera2DComponent &camera,
                        const Vec2 cameraPosition,
                        const std::uint16_t viewportWidth,
                        const std::uint16_t viewportHeight) {
  return {
      .ppu = viewportHeight / std::max(camera.orthographicSize * 2.0F, 1.0F),
      .centerX = viewportWidth * 0.5F,
      .centerY = viewportHeight * 0.5F,
      .camera = cameraPosition,
  };
}

} // namespace

DebugCanvasRenderer::DebugCanvasRenderer(Canvas2D &canvas,
                                         const FontAtlas2D *font)
    : canvas_(canvas), font_(font) {}

bool DebugCanvasRenderer::drawWorld(const World &world,
                                    const Camera2DComponent &camera,
                                    const Vec2 cameraPosition,
                                    const std::uint16_t viewportWidth,
                                    const std::uint16_t viewportHeight) {
  const Projection2D screen =
      projection(camera, cameraPosition, viewportWidth, viewportHeight);
  for (const DebugLine &line : world.debugLines) {
    const Vec2 start = screen.point(line.start);
    const Vec2 end = screen.point(line.end);
    if (!canvas_.line(start.x, start.y, end.x, end.y,
                      std::max(line.width, 1.0F),
                      packVertexColorRgba8(line.color)))
      return false;
  }

  int drawIndex = 0;
  for (const Entity &entity : world.entities) {
    if (!entity.enabled)
      continue;
    const Vec2 worldPosition = worldPosition2D(world, entity);
    const Vec2 entityScreen = screen.point(worldPosition);
    if (world.debug.colliders) {
      if (const auto *box = entity.component<BoxCollider2DComponent>();
          box != nullptr && box->debugVisible) {
        const auto corner = [&](const float x, const float y) {
          return screen.point(worldPoint2D(
              world, entity, {box->offset.x + x, box->offset.y + y}));
        };
        const float halfWidth = box->size.x * 0.5F;
        const float halfHeight = box->size.y * 0.5F;
        const Vec2 corners[4] = {
            corner(-halfWidth, -halfHeight),
            corner(halfWidth, -halfHeight),
            corner(halfWidth, halfHeight),
            corner(-halfWidth, halfHeight),
        };
        for (int index = 0; index < 4; ++index)
          if (!canvas_.line(corners[index].x, corners[index].y,
                            corners[(index + 1) % 4].x,
                            corners[(index + 1) % 4].y, 2.0F, 0xffdce646U))
            return false;
      }
      if (const auto *circle = entity.component<CircleCollider2DComponent>();
          circle != nullptr && circle->debugVisible) {
        const Vec2 center =
            screen.point(worldPoint2D(world, entity, circle->offset));
        const Vec2 scale = worldScale2D(world, entity);
        if (!canvas_.circleOutline(
                center.x, center.y,
                circle->radius *
                    std::max(std::abs(scale.x), std::abs(scale.y)) * screen.ppu,
                2.0F, 0xffdce646U, 32))
          return false;
      }
    }
    if (font_ != nullptr && (world.debug.entityIds || world.debug.drawOrder)) {
      std::string label;
      if (world.debug.drawOrder)
        label = "#" + std::to_string(drawIndex) + " ";
      if (world.debug.entityIds)
        label += entity.id;
      if (!font_->draw(canvas_, label, entityScreen.x + 5.0F,
                       entityScreen.y - 18.0F, 0xff64e2ffU, 0.5F))
        return false;
    }
    ++drawIndex;
  }

  if (world.debug.contacts) {
    for (const PhysicsContact2D &contact : world.physicsContacts) {
      const auto found =
          std::ranges::find_if(world.entities, [&](const Entity &entity) {
            return entity.id == contact.entityId;
          });
      if (found == world.entities.end())
        continue;
      const Vec2 start = screen.point(worldPosition2D(world, *found));
      if (!canvas_.line(start.x, start.y, start.x + contact.normal.x * 28.0F,
                        start.y - contact.normal.y * 28.0F, 2.0F, 0xff5a5affU))
        return false;
    }
  }
  return true;
}

bool DebugCanvasRenderer::drawNavigation(
    const navigation::NavigationGrid2D &grid, const Camera2DComponent &camera,
    const Vec2 cameraPosition, const std::uint16_t viewportWidth,
    const std::uint16_t viewportHeight) {
  if (!grid.available())
    return true;
  const Projection2D screen =
      projection(camera, cameraPosition, viewportWidth, viewportHeight);
  const float size = grid.cellSize() * screen.ppu;
  for (int y = 0; y < grid.height(); ++y) {
    for (int x = 0; x < grid.width(); ++x) {
      const auto worldCenter = grid.cellToWorld({x, y});
      if (!worldCenter)
        continue;
      const Vec2 center = screen.point(*worldCenter);
      const Rect2D cell{.x = center.x - size * 0.5F,
                        .y = center.y - size * 0.5F,
                        .width = size,
                        .height = size};
      if (grid.blocked({x, y}) && !canvas_.solid(cell, 0x465b5bf4U))
        return false;
      if (grid.cost({x, y}) > 1.0F && !canvas_.solid(cell, 0x3c58beeeU))
        return false;
      if (!canvas_.line(cell.x, cell.y, cell.x + cell.width, cell.y, 1.0F,
                        0x82cddc48U) ||
          !canvas_.line(cell.x + cell.width, cell.y, cell.x + cell.width,
                        cell.y + cell.height, 1.0F, 0x82cddc48U) ||
          !canvas_.line(cell.x + cell.width, cell.y + cell.height, cell.x,
                        cell.y + cell.height, 1.0F, 0x82cddc48U) ||
          !canvas_.line(cell.x, cell.y + cell.height, cell.x, cell.y, 1.0F,
                        0x82cddc48U))
        return false;
    }
  }
  return true;
}

} // namespace demi::runtime::render
