#include "editor/EditorSceneView2DState.h"

#include "editor/EditorIsoGridCell.h"
#include "editor/EditorIsoScene2D.h"

#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/IsoTransformComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace demi::editor {

void EditorSceneView2DState::reset(const runtime::World &world) {
  cameraSettings_ = {};
  cameraSettings_.clearColor = {0.055F, 0.07F, 0.09F, 1.0F};
  position_ = {};
  transformSpace_ = EditorTransformSpace::Local;
  capturesPointer_ = false;
  (void)alignToFirstCamera(world);
}

bool EditorSceneView2DState::alignToFirstCamera(const runtime::World &world) {
  const auto found =
      std::ranges::find_if(world.entities, [](const auto &entity) {
        return entity.enabled &&
               entity.template hasComponent<runtime::Camera2DComponent>();
      });
  if (found == world.entities.end())
    return false;
  cameraSettings_ = *found->component<runtime::Camera2DComponent>();
  cameraSettings_.clearColor = {0.055F, 0.07F, 0.09F, 1.0F};
  position_ = runtime::worldPosition2D(world, *found);
  return true;
}

void EditorSceneView2DState::update(const EditorViewportInput &input) {
  const bool canBegin = input.hovered && input.focused;
  if (!capturesPointer_ && canBegin && input.panButton)
    capturesPointer_ = true;
  if (capturesPointer_ && !input.panButton)
    capturesPointer_ = false;
  if (!canBegin && !capturesPointer_)
    return;

  if (input.panButton) {
    const float ppu = pixelsPerUnit(input.viewportSize.y);
    position_.x -= input.mouseDelta.x / ppu;
    position_.y += input.mouseDelta.y / ppu;
  }
  if (input.wheel != 0.0F)
    cameraSettings_.orthographicSize = std::clamp(
        cameraSettings_.orthographicSize * std::exp(-input.wheel * 0.16F),
        0.01F, 10000.0F);
}

bool EditorSceneView2DState::frameEntity(const runtime::World &world,
                                         const std::string_view entityId) {
  const auto found =
      std::ranges::find(world.entities, entityId, &runtime::Entity::id);
  if (found == world.entities.end())
    return false;
  if (found->hasComponent<runtime::IsoTransformComponent>()) {
    const auto visual =
        editorIsoVisual2D(world, *found, camera(), {1600.0F, 900.0F});
    if (!visual)
      return false;
    position_ = visual->worldAnchor;
    cameraSettings_.orthographicSize =
        std::max({(visual->screenMaximum.x - visual->screenMinimum.x) /
                      pixelsPerUnit(900.0F),
                  (visual->screenMaximum.y - visual->screenMinimum.y) /
                      pixelsPerUnit(900.0F),
                  1.0F});
    return true;
  }
  if (!found->hasComponent<runtime::Transform2DComponent>())
    return false;
  position_ = runtime::worldPosition2D(world, *found);
  const runtime::Vec2 scale = runtime::worldScale2D(world, *found);
  runtime::Vec2 size{1.0F, 1.0F};
  if (const auto *sprite = found->component<runtime::SpriteComponent>())
    size = {sprite->size.x > 0.0F ? sprite->size.x : 1.0F,
            sprite->size.y > 0.0F ? sprite->size.y : 1.0F};
  cameraSettings_.orthographicSize =
      std::max({std::abs(size.x * scale.x), std::abs(size.y * scale.y), 1.0F});
  return true;
}

bool EditorSceneView2DState::frameGridCell(const runtime::World &world,
                                           const EditorIsoGridCell &cell) {
  const auto position = isoGridCellWorldPosition(world, cell);
  if (!position)
    return false;
  position_ = *position;
  cameraSettings_.orthographicSize =
      std::max(std::min(cameraSettings_.orthographicSize, 4.0F), 1.0F);
  return true;
}

EditorSceneView2DCamera EditorSceneView2DState::camera() const {
  return {.projection = cameraSettings_, .position = position_};
}

float EditorSceneView2DState::pixelsPerUnit(const float viewportHeight) const {
  return std::max(viewportHeight, 1.0F) /
         std::max(cameraSettings_.orthographicSize * 2.0F, 0.01F);
}

} // namespace demi::editor
