#include "editor/EditorViewportProjection2D.h"

#include "editor/EditorIsoScene2D.h"

#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/BoxCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/CircleCollider2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Tilemap2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <ranges>
#include <tuple>
#include <vector>

namespace demi::editor {
namespace {

float pixelsPerUnit(const EditorSceneView2DCamera &camera,
                    const runtime::Vec2 viewport) {
  return std::max(viewport.y, 1.0F) /
         std::max(camera.projection.orthographicSize * 2.0F, 0.01F);
}

runtime::Vec2 entitySize(const runtime::Entity &entity) {
  if (const auto *sprite = entity.component<runtime::SpriteComponent>())
    return {sprite->size.x > 0.0F ? sprite->size.x : 1.0F,
            sprite->size.y > 0.0F ? sprite->size.y : 1.0F};
  if (const auto *box = entity.component<runtime::BoxCollider2DComponent>())
    return box->size;
  if (const auto *circle =
          entity.component<runtime::CircleCollider2DComponent>())
    return {circle->radius * 2.0F, circle->radius * 2.0F};
  if (entity.hasComponent<runtime::Camera2DComponent>())
    return {1.2F, 0.8F};
  if (entity.hasComponent<runtime::Tilemap2DComponent>())
    return {2.0F, 2.0F};
  return {0.5F, 0.5F};
}

struct PickCandidate {
  const runtime::Entity *entity = nullptr;
  int renderPass = 0;
  int sortingOrder = 0;
  std::string layer;
  float depth = 0.0F;
};

auto drawOrder(const PickCandidate &candidate) {
  return std::tuple{candidate.renderPass, candidate.sortingOrder,
                    candidate.layer, candidate.depth, candidate.entity->id};
}

bool hitsRegularEntity(const runtime::World &world,
                       const runtime::Entity &entity,
                       const runtime::Vec2 point) {
  const runtime::Vec2 position = runtime::worldPosition2D(world, entity);
  const runtime::Vec2 scale = runtime::worldScale2D(world, entity);
  const runtime::Vec2 size = entitySize(entity);
  const runtime::Vec2 local =
      runtime::rotate2D({point.x - position.x, point.y - position.y},
                        -runtime::worldRotation2D(world, entity));
  if (const auto *sprite = entity.component<runtime::SpriteComponent>())
    return local.x >= -sprite->pivot.x * size.x * std::abs(scale.x) &&
           local.x <= (1.0F - sprite->pivot.x) * size.x * std::abs(scale.x) &&
           local.y >= -(1.0F - sprite->pivot.y) * size.y * std::abs(scale.y) &&
           local.y <= sprite->pivot.y * size.y * std::abs(scale.y);
  return std::abs(local.x) <= std::abs(size.x * scale.x) * 0.5F &&
         std::abs(local.y) <= std::abs(size.y * scale.y) * 0.5F;
}

} // namespace

runtime::Vec2 projectScenePoint2D(const EditorSceneView2DCamera &camera,
                                  const runtime::Vec2 worldPoint,
                                  const runtime::Vec2 viewportSize) {
  const float ppu = pixelsPerUnit(camera, viewportSize);
  return {viewportSize.x * 0.5F + (worldPoint.x - camera.position.x) * ppu,
          viewportSize.y * 0.5F - (worldPoint.y - camera.position.y) * ppu};
}

runtime::Vec2 unprojectScenePoint2D(const EditorSceneView2DCamera &camera,
                                    const runtime::Vec2 viewportPoint,
                                    const runtime::Vec2 viewportSize) {
  const float ppu = pixelsPerUnit(camera, viewportSize);
  return {camera.position.x + (viewportPoint.x - viewportSize.x * 0.5F) / ppu,
          camera.position.y - (viewportPoint.y - viewportSize.y * 0.5F) / ppu};
}

std::optional<std::string> pickSceneEntity2D(
    const runtime::World &world, const EditorSceneView2DCamera &camera,
    const runtime::Vec2 viewportPosition, const runtime::Vec2 viewportSize,
    const std::string_view cycleAfterEntityId) {
  const runtime::Vec2 point =
      unprojectScenePoint2D(camera, viewportPosition, viewportSize);
  std::vector<PickCandidate> hits;
  for (const runtime::Entity &entity : world.entities) {
    if (!entity.enabled)
      continue;
    if (const auto visual =
            editorIsoVisual2D(world, entity, camera, viewportSize)) {
      if (editorIsoVisualContains(entity, *visual, viewportPosition))
        hits.push_back({.entity = &entity,
                        .renderPass = 1,
                        .sortingOrder = visual->sortingOrder,
                        .layer = visual->layer,
                        .depth = visual->depth});
      continue;
    }
    if (!entity.hasComponent<runtime::Transform2DComponent>() ||
        !hitsRegularEntity(world, entity, point))
      continue;
    if (const auto *sprite = entity.component<runtime::SpriteComponent>()) {
      hits.push_back({.entity = &entity,
                      .renderPass = 2,
                      .sortingOrder = sprite->sortingOrder,
                      .layer = sprite->layer});
    } else if (const auto *tilemap =
                   entity.component<runtime::Tilemap2DComponent>()) {
      hits.push_back({.entity = &entity,
                      .renderPass = 0,
                      .sortingOrder = tilemap->sortingOrder,
                      .layer = tilemap->layer});
    } else {
      hits.push_back({.entity = &entity, .renderPass = -1});
    }
  }
  if (hits.empty())
    return std::nullopt;
  std::ranges::sort(hits,
                    [](const PickCandidate &left, const PickCandidate &right) {
                      return drawOrder(left) > drawOrder(right);
                    });
  const auto current = std::ranges::find_if(hits, [&](const auto &candidate) {
    return candidate.entity->id == cycleAfterEntityId;
  });
  if (current != hits.end() && hits.size() > 1)
    return std::next(current) == hits.end() ? hits.front().entity->id
                                            : std::next(current)->entity->id;
  return hits.front().entity->id;
}

} // namespace demi::editor
