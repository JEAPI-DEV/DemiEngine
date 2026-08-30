#include "editor/EditorIsoScene2D.h"

#include "editor/EditorViewportProjection2D.h"

#include "demi/runtime/isometric/IsoGridMath.h"
#include "demi/runtime/isometric/IsoWorldQueries.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/IsoTransformComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/gameplay/BuildableComponent.h"

#include <algorithm>
#include <cmath>

namespace demi::editor {

std::optional<EditorIsoVisual2D>
editorIsoVisual2D(const runtime::World &world, const runtime::Entity &entity,
                  const EditorSceneView2DCamera &camera,
                  const runtime::Vec2 viewportSize) {
  const auto grid = runtime::isometric::gridDefinition(world);
  const auto *local = entity.component<runtime::IsoTransformComponent>();
  if (!grid || local == nullptr)
    return std::nullopt;

  const runtime::IsoTransformComponent transform =
      runtime::worldIsoTransform(world, entity);
  runtime::Vec2 renderTile = transform.tile;
  if (entity.hasComponent<runtime::BuildableComponent>()) {
    renderTile.x += (transform.footprint.x - 1.0F) * 0.5F;
    renderTile.y += (transform.footprint.y - 1.0F) * 0.5F;
  }
  runtime::Vec2 worldAnchor =
      runtime::isometric::tileToWorld(*grid, renderTile);
  worldAnchor.y += transform.height;

  const auto *sprite = entity.component<runtime::SpriteComponent>();
  const auto *buildable = entity.component<runtime::BuildableComponent>();
  if (sprite == nullptr && buildable == nullptr)
    return std::nullopt;

  const float footprintScale =
      std::max(transform.footprint.x + transform.footprint.y, 1.0F);
  const float width = sprite != nullptr && sprite->size.x > 0.0F
                          ? sprite->size.x
                          : footprintScale * 0.62F;
  const float height = sprite != nullptr && sprite->size.y > 0.0F
                           ? sprite->size.y
                           : footprintScale * grid->cellHeight * 0.95F;
  const float bottomOffset = footprintScale * grid->cellHeight * 0.5F;
  const float pivotX = sprite != nullptr ? sprite->pivot.x : 0.5F;
  const float pivotY = sprite != nullptr ? sprite->pivot.y : 1.0F;
  const float pixelsPerUnit =
      std::max(viewportSize.y, 1.0F) /
      std::max(camera.projection.orthographicSize * 2.0F, 0.01F);
  runtime::Vec2 screenAnchor =
      projectScenePoint2D(camera, worldAnchor, viewportSize);
  screenAnchor.y += bottomOffset * pixelsPerUnit;
  return EditorIsoVisual2D{
      .worldAnchor = worldAnchor,
      .screenMinimum = {screenAnchor.x - pivotX * width * pixelsPerUnit,
                        screenAnchor.y - pivotY * height * pixelsPerUnit},
      .screenMaximum = {screenAnchor.x +
                            (1.0F - pivotX) * width * pixelsPerUnit,
                        screenAnchor.y +
                            (1.0F - pivotY) * height * pixelsPerUnit},
      .sortingOrder = sprite != nullptr ? sprite->sortingOrder : 0,
      .layer = sprite != nullptr ? sprite->layer : std::string{},
      .depth = transform.tile.x + transform.tile.y + transform.height};
}

bool editorIsoVisualContains(const runtime::Entity &entity,
                             const EditorIsoVisual2D &visual,
                             const runtime::Vec2 viewportPoint) {
  if (viewportPoint.x < visual.screenMinimum.x ||
      viewportPoint.x > visual.screenMaximum.x ||
      viewportPoint.y < visual.screenMinimum.y ||
      viewportPoint.y > visual.screenMaximum.y)
    return false;

  const auto *sprite = entity.component<runtime::SpriteComponent>();
  if (sprite == nullptr || sprite->shape == "rectangle")
    return true;
  const float width = visual.screenMaximum.x - visual.screenMinimum.x;
  const float height = visual.screenMaximum.y - visual.screenMinimum.y;
  const float normalizedX =
      (viewportPoint.x - visual.screenMinimum.x) / std::max(width, 0.001F);
  const float normalizedY =
      (viewportPoint.y - visual.screenMinimum.y) / std::max(height, 0.001F);
  if (sprite->shape == "circle") {
    const float x = normalizedX * 2.0F - 1.0F;
    const float y = normalizedY * 2.0F - 1.0F;
    return x * x + y * y <= 1.0F;
  }
  if (sprite->shape == "triangle")
    return normalizedY >= std::abs(normalizedX * 2.0F - 1.0F);
  return true;
}

} // namespace demi::editor
