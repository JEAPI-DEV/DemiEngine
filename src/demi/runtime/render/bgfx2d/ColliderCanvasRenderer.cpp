#include "demi/runtime/render/bgfx2d/ColliderCanvasRenderer.h"

#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/BoxCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/CapsuleCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/CircleCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/EdgeCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/IsoTransformComponent.h"
#include "demi/runtime/scene/components/2dcomponents/PolygonCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Rigidbody2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Tilemap2DComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace demi::runtime::render {
namespace {

constexpr std::uint32_t StaticFill = 0xff608860U;
constexpr std::uint32_t DynamicFill = 0xff3780baU;
constexpr std::uint32_t ColliderOutline = 0xff695bf4U;

struct EntityProjection2D {
  float pixelsPerUnit;
  float centerX;
  float centerY;
  Vec2 camera;
  Vec2 position;
  Vec2 scale;
  float cosine;
  float sine;

  [[nodiscard]] Vec2 point(const Vec2 local) const {
    const Vec2 scaled{local.x * scale.x, local.y * scale.y};
    const Vec2 rotated{scaled.x * cosine - scaled.y * sine,
                       scaled.x * sine + scaled.y * cosine};
    return {
        centerX + (position.x + rotated.x - camera.x) * pixelsPerUnit,
        centerY - (position.y + rotated.y - camera.y) * pixelsPerUnit,
    };
  }

  [[nodiscard]] Vec2 worldUnitPoint(const Vec2 local) const {
    const Vec2 rotated{local.x * cosine - local.y * sine,
                       local.x * sine + local.y * cosine};
    return {
        centerX + (position.x + rotated.x - camera.x) * pixelsPerUnit,
        centerY - (position.y + rotated.y - camera.y) * pixelsPerUnit,
    };
  }
};

EntityProjection2D projection(const World &world, const Entity &entity,
                              const Camera2DComponent &camera,
                              const Vec2 cameraPosition,
                              const std::uint16_t viewportWidth,
                              const std::uint16_t viewportHeight) {
  const float rotation = worldRotation2D(world, entity);
  return {
      .pixelsPerUnit =
          viewportHeight / std::max(camera.orthographicSize * 2.0F, 1.0F),
      .centerX = viewportWidth * 0.5F,
      .centerY = viewportHeight * 0.5F,
      .camera = cameraPosition,
      .position = worldPosition2D(world, entity),
      .scale = worldScale2D(world, entity),
      .cosine = std::cos(rotation),
      .sine = std::sin(rotation),
  };
}

bool lineLoop(Canvas2D &canvas, const std::vector<Vec2> &points,
              const bool loop) {
  for (std::size_t index = 1; index < points.size(); ++index)
    if (!canvas.line(points[index - 1].x, points[index - 1].y, points[index].x,
                     points[index].y, 1.0F, ColliderOutline))
      return false;
  return !loop || points.size() < 2 ||
         canvas.line(points.back().x, points.back().y, points.front().x,
                     points.front().y, 1.0F, ColliderOutline);
}

bool drawBox(Canvas2D &canvas, const EntityProjection2D &screen,
             const BoxCollider2DComponent &box, const std::uint32_t fill) {
  const Vec2 center = screen.point(box.offset);
  const float width =
      box.size.x * std::abs(screen.scale.x) * screen.pixelsPerUnit;
  const float height =
      box.size.y * std::abs(screen.scale.y) * screen.pixelsPerUnit;
  if (!canvas.imageTransformed(
          canvas.whiteTexture(), center.x, center.y, width, height, 0.5F, 0.5F,
          -std::atan2(screen.sine, screen.cosine), {}, fill))
    return false;

  const Vec2 half{box.size.x * 0.5F, box.size.y * 0.5F};
  const std::array<Vec2, 4> corners{
      screen.point({box.offset.x - half.x, box.offset.y - half.y}),
      screen.point({box.offset.x + half.x, box.offset.y - half.y}),
      screen.point({box.offset.x + half.x, box.offset.y + half.y}),
      screen.point({box.offset.x - half.x, box.offset.y + half.y}),
  };
  for (std::size_t index = 0; index < corners.size(); ++index)
    if (!canvas.line(corners[index].x, corners[index].y,
                     corners[(index + 1) % corners.size()].x,
                     corners[(index + 1) % corners.size()].y, 1.0F,
                     ColliderOutline))
      return false;
  return true;
}

bool drawCircle(Canvas2D &canvas, const EntityProjection2D &screen,
                const CircleCollider2DComponent &circle,
                const std::uint32_t fill) {
  const Vec2 center = screen.point(circle.offset);
  const float radius =
      circle.radius *
      std::max(std::abs(screen.scale.x), std::abs(screen.scale.y)) *
      screen.pixelsPerUnit;
  return canvas.circle(center.x, center.y, radius, fill) &&
         canvas.circleOutline(center.x, center.y, radius, 1.0F,
                              ColliderOutline);
}

bool drawCapsule(Canvas2D &canvas, const EntityProjection2D &screen,
                 const CapsuleCollider2DComponent &capsule,
                 const std::uint32_t fill) {
  const Vec2 size{capsule.size.x * std::abs(screen.scale.x),
                  capsule.size.y * std::abs(screen.scale.y)};
  const Vec2 offset{capsule.offset.x * screen.scale.x,
                    capsule.offset.y * screen.scale.y};
  const bool vertical = size.y >= size.x;
  const float radius = std::max(std::min(size.x, size.y) * 0.5F, 0.0F);
  const float halfSegment =
      std::max((vertical ? size.y : size.x) * 0.5F - radius, 0.0F);
  const Vec2 start =
      screen.worldUnitPoint({offset.x - (vertical ? 0.0F : halfSegment),
                             offset.y - (vertical ? halfSegment : 0.0F)});
  const Vec2 end =
      screen.worldUnitPoint({offset.x + (vertical ? 0.0F : halfSegment),
                             offset.y + (vertical ? halfSegment : 0.0F)});
  const float screenRadius = radius * screen.pixelsPerUnit;
  return canvas.line(start.x, start.y, end.x, end.y, screenRadius * 2.0F,
                     fill) &&
         canvas.circle(start.x, start.y, screenRadius, fill) &&
         canvas.circle(end.x, end.y, screenRadius, fill) &&
         canvas.circleOutline(start.x, start.y, screenRadius, 1.0F,
                              ColliderOutline) &&
         canvas.circleOutline(end.x, end.y, screenRadius, 1.0F,
                              ColliderOutline);
}

} // namespace

ColliderCanvasRenderer::ColliderCanvasRenderer(Canvas2D &canvas)
    : canvas_(canvas) {}

bool ColliderCanvasRenderer::draw(const World &world,
                                  const Camera2DComponent &camera,
                                  const Vec2 cameraPosition,
                                  const std::uint16_t viewportWidth,
                                  const std::uint16_t viewportHeight) {
  for (const Entity &entity : world.entities) {
    if (!entity.enabled || entity.hasComponent<SpriteComponent>() ||
        entity.hasComponent<Tilemap2DComponent>() ||
        entity.hasComponent<IsoTransformComponent>())
      continue;

    const EntityProjection2D screen = projection(
        world, entity, camera, cameraPosition, viewportWidth, viewportHeight);
    const auto *body = entity.component<Rigidbody2DComponent>();
    const std::uint32_t fill = body != nullptr && body->bodyType == "static"
                                   ? StaticFill
                                   : DynamicFill;
    if (const auto *box = entity.component<BoxCollider2DComponent>();
        box != nullptr && box->debugVisible) {
      if (!drawBox(canvas_, screen, *box, fill))
        return false;
      continue;
    }
    if (const auto *circle = entity.component<CircleCollider2DComponent>();
        circle != nullptr && circle->debugVisible) {
      if (!drawCircle(canvas_, screen, *circle, fill))
        return false;
      continue;
    }
    if (const auto *capsule = entity.component<CapsuleCollider2DComponent>();
        capsule != nullptr && capsule->debugVisible) {
      if (!drawCapsule(canvas_, screen, *capsule, fill))
        return false;
      continue;
    }

    const auto *polygon = entity.component<PolygonCollider2DComponent>();
    const auto *edge = entity.component<EdgeCollider2DComponent>();
    const std::vector<Vec2> *source =
        polygon != nullptr && polygon->debugVisible ? &polygon->points
        : edge != nullptr && edge->debugVisible     ? &edge->points
                                                    : nullptr;
    if (source == nullptr)
      continue;
    const Vec2 offset = polygon != nullptr ? polygon->offset : Vec2{};
    std::vector<Vec2> points;
    points.reserve(source->size());
    for (const Vec2 point : *source)
      points.push_back(screen.point({point.x + offset.x, point.y + offset.y}));
    if (!lineLoop(canvas_, points, polygon != nullptr || edge->loop))
      return false;
  }
  return true;
}

} // namespace demi::runtime::render
