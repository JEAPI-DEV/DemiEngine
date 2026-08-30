#include "editor/EditorViewportTool2D.h"

#include "editor/EditorIsoGridCell.h"
#include "editor/EditorIsoGridCellDocument.h"
#include "editor/EditorIsoScene2D.h"
#include "editor/EditorViewportProjection2D.h"

#include "demi/runtime/isometric/IsoGridMath.h"
#include "demi/runtime/isometric/IsoWorldQueries.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/IsoGridComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace demi::editor {
namespace {

float pointSegmentDistance(const runtime::Vec2 point, const runtime::Vec2 start,
                           const runtime::Vec2 end) {
  const runtime::Vec2 line{end.x - start.x, end.y - start.y};
  const float lengthSquared = line.x * line.x + line.y * line.y;
  const float amount = lengthSquared > 0.0001F
                           ? std::clamp(((point.x - start.x) * line.x +
                                         (point.y - start.y) * line.y) /
                                            lengthSquared,
                                        0.0F, 1.0F)
                           : 0.0F;
  const float x = point.x - (start.x + line.x * amount);
  const float y = point.y - (start.y + line.y * amount);
  return std::sqrt(x * x + y * y);
}

float snap(const float value, const float increment, const bool bypass) {
  return bypass || increment <= 0.000001F
             ? value
             : std::round(value / increment) * increment;
}

runtime::Vec2 axisVector(const EditorGizmoAxis axis) {
  return axis == EditorGizmoAxis::Y ? runtime::Vec2{0.0F, 1.0F}
                                    : runtime::Vec2{1.0F, 0.0F};
}

runtime::Vec2 worldToLocalPosition(const runtime::World &world,
                                   const runtime::Entity &entity,
                                   const runtime::Vec2 worldPosition) {
  const auto *transform = entity.component<runtime::Transform2DComponent>();
  const runtime::Entity *parent =
      transform != nullptr && !transform->parent.empty()
          ? runtime::findEntity(world, transform->parent)
          : nullptr;
  if (parent == nullptr ||
      !parent->hasComponent<runtime::Transform2DComponent>())
    return worldPosition;
  const runtime::Vec2 parentPosition = runtime::worldPosition2D(world, *parent);
  const runtime::Vec2 parentScale = runtime::worldScale2D(world, *parent);
  runtime::Vec2 local = runtime::rotate2D(
      {worldPosition.x - parentPosition.x, worldPosition.y - parentPosition.y},
      -runtime::worldRotation2D(world, *parent));
  if (std::abs(parentScale.x) > 0.000001F)
    local.x /= parentScale.x;
  if (std::abs(parentScale.y) > 0.000001F)
    local.y /= parentScale.y;
  return local;
}

float worldToLocalRotation(const runtime::World &world,
                           const runtime::Entity &entity,
                           const float worldRotation) {
  const auto *transform = entity.component<runtime::Transform2DComponent>();
  const runtime::Entity *parent =
      transform != nullptr && !transform->parent.empty()
          ? runtime::findEntity(world, transform->parent)
          : nullptr;
  return parent != nullptr &&
                 parent->hasComponent<runtime::Transform2DComponent>()
             ? worldRotation - runtime::worldRotation2D(world, *parent)
             : worldRotation;
}

runtime::Vec2 worldToLocalScale(const runtime::World &world,
                                const runtime::Entity &entity,
                                runtime::Vec2 worldScale) {
  const auto *transform = entity.component<runtime::Transform2DComponent>();
  const runtime::Entity *parent =
      transform != nullptr && !transform->parent.empty()
          ? runtime::findEntity(world, transform->parent)
          : nullptr;
  if (parent == nullptr ||
      !parent->hasComponent<runtime::Transform2DComponent>())
    return worldScale;
  const runtime::Vec2 parentScale = runtime::worldScale2D(world, *parent);
  if (std::abs(parentScale.x) > 0.000001F)
    worldScale.x /= parentScale.x;
  if (std::abs(parentScale.y) > 0.000001F)
    worldScale.y /= parentScale.y;
  return worldScale;
}

} // namespace

EditorGizmoPresentation EditorViewportTool2D::presentation(
    const runtime::World &world, const std::string_view selectedEntityId,
    const EditorSceneView2DState &sceneView, const runtime::Vec2 viewportSize,
    const std::optional<EditorIsoGridCell> &selectedGridCell) const {
  EditorGizmoPresentation result;
  if (selectedGridCell) {
    const auto origin = isoGridCellWorldPosition(world, *selectedGridCell);
    const auto grid = runtime::isometric::gridDefinition(world);
    if (!origin || !grid || operation_ != EditorGizmoOperation::Translate)
      return result;
    result.origin =
        projectScenePoint2D(sceneView.camera(), *origin, viewportSize);
    const runtime::Vec2 worldOrigin =
        runtime::isometric::tileToWorld(*grid, runtime::Vec2{});
    for (const EditorGizmoAxis axis :
         {EditorGizmoAxis::X, EditorGizmoAxis::Y}) {
      const runtime::Vec2 worldStep =
          runtime::isometric::tileToWorld(*grid, axisVector(axis));
      const runtime::Vec2 endWorld{origin->x + worldStep.x - worldOrigin.x,
                                   origin->y + worldStep.y - worldOrigin.y};
      result.axes.push_back({.axis = axis,
                             .start = result.origin,
                             .end = projectScenePoint2D(
                                 sceneView.camera(), endWorld, viewportSize)});
    }
    return result;
  }
  const auto entity =
      std::ranges::find(world.entities, selectedEntityId, &runtime::Entity::id);
  if (entity == world.entities.end())
    return result;
  if (entity->hasComponent<runtime::IsoTransformComponent>()) {
    if (operation_ != EditorGizmoOperation::Translate)
      return result;
    const auto visual =
        editorIsoVisual2D(world, *entity, sceneView.camera(), viewportSize);
    const auto grid = runtime::isometric::gridDefinition(world);
    if (!visual || !grid)
      return result;
    result.origin = projectScenePoint2D(sceneView.camera(), visual->worldAnchor,
                                        viewportSize);
    for (const EditorGizmoAxis axis :
         {EditorGizmoAxis::X, EditorGizmoAxis::Y}) {
      const runtime::Vec2 tileAxis = axisVector(axis);
      const runtime::Vec2 worldStep =
          runtime::isometric::tileToWorld(*grid, tileAxis);
      const runtime::Vec2 worldOrigin =
          runtime::isometric::tileToWorld(*grid, runtime::Vec2{});
      const runtime::Vec2 endWorld{
          visual->worldAnchor.x + worldStep.x - worldOrigin.x,
          visual->worldAnchor.y + worldStep.y - worldOrigin.y};
      result.axes.push_back({.axis = axis,
                             .start = result.origin,
                             .end = projectScenePoint2D(
                                 sceneView.camera(), endWorld, viewportSize)});
    }
    return result;
  }
  if (!entity->hasComponent<runtime::Transform2DComponent>())
    return result;
  const runtime::Vec2 position = runtime::worldPosition2D(world, *entity);
  result.origin =
      projectScenePoint2D(sceneView.camera(), position, viewportSize);
  const float length = 72.0F / sceneView.pixelsPerUnit(viewportSize.y);
  if (operation_ == EditorGizmoOperation::Rotate) {
    const runtime::Vec2 end = projectScenePoint2D(
        sceneView.camera(), {position.x + length, position.y}, viewportSize);
    result.axes.push_back(
        {.axis = EditorGizmoAxis::Z, .start = result.origin, .end = end});
    return result;
  }
  for (const EditorGizmoAxis axis : {EditorGizmoAxis::X, EditorGizmoAxis::Y}) {
    runtime::Vec2 direction = axisVector(axis);
    if (sceneView.transformSpace() == EditorTransformSpace::Local)
      direction = runtime::rotate2D(direction,
                                    runtime::worldRotation2D(world, *entity));
    const runtime::Vec2 end = projectScenePoint2D(
        sceneView.camera(),
        {position.x + direction.x * length, position.y + direction.y * length},
        viewportSize);
    result.axes.push_back({.axis = axis, .start = result.origin, .end = end});
  }
  return result;
}

EditorViewportToolAction EditorViewportTool2D::update(
    const runtime::World &world, const std::string_view selectedEntityId,
    const EditorSceneView2DState &sceneView,
    const EditorViewportToolInput &input,
    const std::optional<EditorIsoGridCell> &selectedGridCell) {
  EditorViewportToolAction action;
  if (active_) {
    const auto entity = std::ranges::find(world.entities, active_->entityId,
                                          &runtime::Entity::id);
    if (input.cancelPressed || !input.focused ||
        selectedEntityId != active_->entityId ||
        entity == world.entities.end() ||
        (!active_->isGridCell &&
         !entity->hasComponent<runtime::Transform2DComponent>() &&
         !entity->hasComponent<runtime::IsoTransformComponent>())) {
      active_.reset();
      action.completion = EditorDragCompletion::Cancel;
      return action;
    }
    if (input.leftReleased || !input.leftDown) {
      active_.reset();
      action.completion = EditorDragCompletion::Finish;
      return action;
    }
    active_->pixels += input.mouseDelta.x * active_->screenDirection.x +
                       input.mouseDelta.y * active_->screenDirection.y;
    if (active_->isGridCell) {
      const int amount = static_cast<int>(
          std::lround(active_->pixels / active_->pixelsPerStep));
      EditorIsoGridCell moved = active_->gridCell;
      (active_->axis == EditorGizmoAxis::Y ? moved.y : moved.x) += amount;
      const auto *grid = entity->component<runtime::IsoGridComponent>();
      if (grid == nullptr)
        return action;
      const nlohmann::json component{
          {"cell_textures", active_->initialCellTextures}};
      std::string ignored;
      auto cells =
          moveAuthoredIsoGridCell(component, active_->gridCell, moved.x,
                                  moved.y, grid->width, grid->height, ignored);
      if (!cells)
        return action;
      action.edit =
          EditorViewportEdit{.target = {.entityId = moved.gridEntityId,
                                        .component = "IsoGrid",
                                        .field = "cell_textures"},
                             .value = std::move(*cells)};
      action.isoGridCellSelectionChanged = true;
      action.selectedIsoGridCell = std::move(moved);
      return action;
    }
    if (active_->isIsometric) {
      const float amount =
          snap(active_->pixels / active_->pixelsPerStep,
               sceneView.translationSnap, input.bypassSnapping);
      runtime::Vec2 tile = active_->initialIsoTile;
      (active_->axis == EditorGizmoAxis::Y ? tile.y : tile.x) += amount;
      action.edit = EditorViewportEdit{.target = {.entityId = active_->entityId,
                                                  .component = "IsoTransform",
                                                  .field = "tile"},
                                       .value = {tile.x, tile.y}};
      return action;
    }
    if (active_->operation == EditorGizmoOperation::Translate) {
      const float amount =
          snap(active_->pixels / active_->pixelsPerStep,
               sceneView.translationSnap, input.bypassSnapping);
      const runtime::Vec2 desired{
          active_->initialWorldPosition.x + active_->worldAxis.x * amount,
          active_->initialWorldPosition.y + active_->worldAxis.y * amount};
      const runtime::Vec2 local = worldToLocalPosition(world, *entity, desired);
      action.edit = EditorViewportEdit{.target = {.entityId = active_->entityId,
                                                  .component = "Transform2D",
                                                  .field = "position"},
                                       .value = {local.x, local.y}};
    } else if (active_->operation == EditorGizmoOperation::Rotate) {
      constexpr float DegreesToRadians = 0.01745329251994329577F;
      const float amount =
          snap(active_->pixels * 0.01F,
               sceneView.rotationSnapDegrees * DegreesToRadians,
               input.bypassSnapping);
      const float local = worldToLocalRotation(
          world, *entity, active_->initialWorldRotation + amount);
      action.edit = EditorViewportEdit{.target = {.entityId = active_->entityId,
                                                  .component = "Transform2D",
                                                  .field = "rotation"},
                                       .value = local};
    } else {
      const float amount = snap(active_->pixels * 0.01F, sceneView.scaleSnap,
                                input.bypassSnapping);
      runtime::Vec2 local = active_->initialLocal.scale;
      runtime::Vec2 desired = active_->initialWorldScale;
      runtime::Vec2 &scale =
          sceneView.transformSpace() == EditorTransformSpace::Local ? local
                                                                    : desired;
      float &value = active_->axis == EditorGizmoAxis::Y ? scale.y : scale.x;
      const float initial = value;
      value = std::max(initial + amount, 0.01F);
      if (sceneView.transformSpace() == EditorTransformSpace::World)
        local = worldToLocalScale(world, *entity, desired);
      action.edit = EditorViewportEdit{.target = {.entityId = active_->entityId,
                                                  .component = "Transform2D",
                                                  .field = "scale"},
                                       .value = {local.x, local.y}};
    }
    return action;
  }

  if (!input.hovered || !input.focused || !input.leftPressed ||
      input.navigationModifier)
    return action;
  const EditorGizmoPresentation gizmo = presentation(
      world, selectedEntityId, sceneView, input.viewportSize, selectedGridCell);
  const auto hit = std::ranges::min_element(
      gizmo.axes, {}, [&](const EditorGizmoLine &line) {
        return pointSegmentDistance(input.mousePosition, line.start, line.end);
      });
  if (hit != gizmo.axes.end() &&
      pointSegmentDistance(input.mousePosition, hit->start, hit->end) <=
          10.0F) {
    const auto entity = std::ranges::find(world.entities, selectedEntityId,
                                          &runtime::Entity::id);
    if (entity != world.entities.end()) {
      if (selectedGridCell) {
        const auto *grid = entity->component<runtime::IsoGridComponent>();
        if (grid != nullptr) {
          nlohmann::json cells = nlohmann::json::object();
          for (const auto &[key, texture] : grid->cellTextures)
            cells[key] = texture;
          const runtime::Vec2 screen{hit->end.x - hit->start.x,
                                     hit->end.y - hit->start.y};
          const float screenLength =
              std::sqrt(screen.x * screen.x + screen.y * screen.y);
          active_ = ActiveDrag{.entityId = entity->id,
                               .operation = EditorGizmoOperation::Translate,
                               .axis = hit->axis,
                               .isGridCell = true,
                               .gridCell = *selectedGridCell,
                               .initialCellTextures = std::move(cells),
                               .screenDirection =
                                   screenLength > 0.001F
                                       ? runtime::Vec2{screen.x / screenLength,
                                                       screen.y / screenLength}
                                       : runtime::Vec2{1.0F, 0.0F},
                               .pixelsPerStep = std::max(screenLength, 0.001F)};
          return action;
        }
      }
      const auto *local = entity->component<runtime::Transform2DComponent>();
      const auto *iso = entity->component<runtime::IsoTransformComponent>();
      if (local != nullptr ||
          (iso != nullptr && operation_ == EditorGizmoOperation::Translate)) {
        const runtime::Vec2 screen{hit->end.x - hit->start.x,
                                   hit->end.y - hit->start.y};
        const float screenLength =
            std::sqrt(screen.x * screen.x + screen.y * screen.y);
        runtime::Vec2 axis = axisVector(hit->axis);
        if (sceneView.transformSpace() == EditorTransformSpace::Local &&
            hit->axis != EditorGizmoAxis::Z)
          axis =
              runtime::rotate2D(axis, runtime::worldRotation2D(world, *entity));
        active_ = ActiveDrag{
            .entityId = entity->id,
            .operation = operation_,
            .axis = hit->axis,
            .isIsometric = iso != nullptr,
            .initialLocal =
                local != nullptr ? *local : runtime::Transform2DComponent{},
            .initialIsoTile = iso != nullptr ? iso->tile : runtime::Vec2{},
            .initialWorldPosition =
                local != nullptr ? runtime::worldPosition2D(world, *entity)
                                 : runtime::Vec2{},
            .initialWorldScale = local != nullptr
                                     ? runtime::worldScale2D(world, *entity)
                                     : runtime::Vec2{1.0F, 1.0F},
            .initialWorldRotation =
                local != nullptr ? runtime::worldRotation2D(world, *entity)
                                 : 0.0F,
            .worldAxis = axis,
            .screenDirection = screenLength > 0.001F
                                   ? runtime::Vec2{screen.x / screenLength,
                                                   screen.y / screenLength}
                                   : runtime::Vec2{1.0F, 0.0F},
            .pixelsPerStep =
                iso != nullptr ? std::max(screenLength, 0.001F)
                               : sceneView.pixelsPerUnit(input.viewportSize.y)};
        return action;
      }
    }
  }
  action.selectionChanged = true;
  const auto entity =
      pickSceneEntity2D(world, sceneView.camera(), input.mousePosition,
                        input.viewportSize, selectedEntityId);
  if (entity) {
    action.selectedEntityId = *entity;
    action.isoGridCellSelectionChanged = true;
  } else if (const auto cell = pickPaintedIsoGridCell(world, sceneView.camera(),
                                                      input.mousePosition,
                                                      input.viewportSize)) {
    action.selectionChanged = false;
    action.isoGridCellSelectionChanged = true;
    action.selectedIsoGridCell = *cell;
  } else {
    action.isoGridCellSelectionChanged = true;
  }
  return action;
}

} // namespace demi::editor
