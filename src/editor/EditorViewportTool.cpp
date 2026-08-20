#include "editor/EditorViewportTool.h"
#include "editor/EditorViewportProjection.h"

#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace demi::editor {
namespace {

float dot(const runtime::Vec3 left, const runtime::Vec3 right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

runtime::Vec3 add(const runtime::Vec3 left, const runtime::Vec3 right) {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

runtime::Vec3 subtract(const runtime::Vec3 left, const runtime::Vec3 right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

runtime::Vec3 multiply(const runtime::Vec3 value, const float scalar) {
  return {value.x * scalar, value.y * scalar, value.z * scalar};
}

float length(const runtime::Vec3 value) { return std::sqrt(dot(value, value)); }

runtime::Vec3 normalized(const runtime::Vec3 value,
                         const runtime::Vec3 fallback = {}) {
  const float magnitude = length(value);
  return magnitude > 0.00001F ? multiply(value, 1.0F / magnitude) : fallback;
}

float pointSegmentDistance(const runtime::Vec2 point, const runtime::Vec2 start,
                           const runtime::Vec2 end) {
  const float x = end.x - start.x;
  const float y = end.y - start.y;
  const float lengthSquared = x * x + y * y;
  const float amount =
      lengthSquared > 0.0001F
          ? std::clamp(((point.x - start.x) * x + (point.y - start.y) * y) /
                           lengthSquared,
                       0.0F, 1.0F)
          : 0.0F;
  const float dx = point.x - (start.x + x * amount);
  const float dy = point.y - (start.y + y * amount);
  return std::sqrt(dx * dx + dy * dy);
}

runtime::Vec3 axisVector(const EditorGizmoAxis axis) {
  switch (axis) {
  case EditorGizmoAxis::X:
    return {1.0F, 0.0F, 0.0F};
  case EditorGizmoAxis::Y:
    return {0.0F, 1.0F, 0.0F};
  case EditorGizmoAxis::Z:
    return {0.0F, 0.0F, 1.0F};
  }
  return {};
}

float &axisValue(runtime::Vec3 &value, const EditorGizmoAxis axis) {
  if (axis == EditorGizmoAxis::X)
    return value.x;
  if (axis == EditorGizmoAxis::Y)
    return value.y;
  return value.z;
}

float snapped(const float value, const float increment) {
  return increment > 0.000001F ? std::round(value / increment) * increment
                               : value;
}

} // namespace

EditorGizmoPresentation
EditorViewportTool::presentation(const runtime::World &world,
                                 const std::string_view selectedEntityId,
                                 const EditorSceneViewState &sceneView,
                                 const runtime::Vec2 viewportSize) const {
  EditorGizmoPresentation result;
  const auto found =
      std::ranges::find(world.entities, selectedEntityId, &runtime::Entity::id);
  if (found == world.entities.end())
    return result;
  const auto transform = runtime::resolveWorldTransform3D(world, *found);
  if (!transform)
    return result;
  const EditorSceneViewCamera camera = sceneView.camera();
  const auto origin =
      projectScenePoint3D(camera, transform->position, viewportSize);
  if (!origin)
    return result;
  result.origin = *origin;
  const float cameraDistance =
      length(subtract(transform->position, camera.position));
  const float gizmoLength =
      camera.projection.perspective
          ? std::max(cameraDistance * 0.12F, 0.35F)
          : std::max(camera.projection.orthographicSize * 0.15F, 0.35F);
  for (const EditorGizmoAxis axis :
       {EditorGizmoAxis::X, EditorGizmoAxis::Y, EditorGizmoAxis::Z}) {
    runtime::Vec3 direction = axisVector(axis);
    if (sceneView.transformSpace() == EditorTransformSpace::Local)
      direction = normalized(
          runtime::transformDirection3D(*transform, direction), direction);
    const auto endpoint = projectScenePoint3D(
        camera, add(transform->position, multiply(direction, gizmoLength)),
        viewportSize);
    if (endpoint)
      result.axes.push_back({.axis = axis, .start = *origin, .end = *endpoint});
  }
  return result;
}

EditorViewportToolAction
EditorViewportTool::update(const runtime::World &world,
                           const std::string_view selectedEntityId,
                           const EditorSceneViewState &sceneView,
                           const EditorViewportToolInput &input) {
  EditorViewportToolAction action;
  if (active_) {
    const auto entity = std::ranges::find(world.entities, active_->entityId,
                                          &runtime::Entity::id);
    if (input.cancelPressed || !input.focused ||
        selectedEntityId != active_->entityId ||
        entity == world.entities.end() ||
        !entity->hasComponent<runtime::Transform3DComponent>()) {
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
    runtime::Transform3DComponent local = active_->initialLocal;
    if (active_->operation == EditorGizmoOperation::Translate) {
      const float amount =
          snapped(active_->pixels * 0.01F, sceneView.translationSnap);
      runtime::WorldTransform3D desired = active_->initialWorld;
      desired.position =
          add(desired.position, multiply(active_->worldAxis, amount));
      const auto converted =
          runtime::worldToLocalTransform3D(world, *entity, desired);
      if (!converted) {
        active_.reset();
        action.completion = EditorDragCompletion::Cancel;
        return action;
      }
      local.position = converted->position;
      action.edit = EditorViewportEdit{
          .target = {.entityId = active_->entityId,
                     .component = "Transform3D",
                     .field = "position"},
          .value = {local.position.x, local.position.y, local.position.z}};
    } else if (active_->operation == EditorGizmoOperation::Rotate) {
      constexpr float DegreesToRadians = 0.01745329251994329577F;
      const float increment = sceneView.rotationSnapDegrees * DegreesToRadians;
      const float amount = snapped(active_->pixels * 0.01F, increment);
      if (sceneView.transformSpace() == EditorTransformSpace::Local) {
        local.rotation = runtime::rotateLocalEuler3D(
            active_->initialLocal.rotation, axisVector(active_->axis), amount);
      } else {
        runtime::WorldTransform3D desired = active_->initialWorld;
        desired.rotation = runtime::rotateWorldEuler3D(
            active_->initialWorld.rotation, axisVector(active_->axis), amount);
        const auto converted =
            runtime::worldToLocalTransform3D(world, *entity, desired);
        if (!converted) {
          active_.reset();
          action.completion = EditorDragCompletion::Cancel;
          return action;
        }
        local.rotation = converted->rotation;
      }
      action.edit = EditorViewportEdit{
          .target = {.entityId = active_->entityId,
                     .component = "Transform3D",
                     .field = "rotation"},
          .value = {local.rotation.x, local.rotation.y, local.rotation.z}};
    } else {
      const float amount =
          snapped(active_->pixels * 0.01F, sceneView.scaleSnap);
      if (sceneView.transformSpace() == EditorTransformSpace::Local) {
        axisValue(local.scale, active_->axis) = std::max(
            axisValue(active_->initialLocal.scale, active_->axis) + amount,
            0.01F);
      } else {
        runtime::WorldTransform3D desired = active_->initialWorld;
        axisValue(desired.scale, active_->axis) = std::max(
            axisValue(active_->initialWorld.scale, active_->axis) + amount,
            0.01F);
        const auto converted =
            runtime::worldToLocalTransform3D(world, *entity, desired);
        if (!converted) {
          active_.reset();
          action.completion = EditorDragCompletion::Cancel;
          return action;
        }
        local.scale = converted->scale;
      }
      action.edit = EditorViewportEdit{
          .target = {.entityId = active_->entityId,
                     .component = "Transform3D",
                     .field = "scale"},
          .value = {local.scale.x, local.scale.y, local.scale.z}};
    }
    return action;
  }

  if (!input.hovered || !input.focused || !input.leftPressed ||
      input.navigationModifier)
    return action;

  const EditorGizmoPresentation gizmo =
      presentation(world, selectedEntityId, sceneView, input.viewportSize);
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
      const auto *local = entity->component<runtime::Transform3DComponent>();
      const auto worldTransform =
          runtime::resolveWorldTransform3D(world, *entity);
      if (local != nullptr && worldTransform) {
        const float screenX = hit->end.x - hit->start.x;
        const float screenY = hit->end.y - hit->start.y;
        const float screenLength =
            std::sqrt(screenX * screenX + screenY * screenY);
        runtime::Vec3 worldAxis = axisVector(hit->axis);
        if (sceneView.transformSpace() == EditorTransformSpace::Local)
          worldAxis = normalized(
              runtime::transformDirection3D(*worldTransform, worldAxis),
              worldAxis);
        active_ = ActiveDrag{.entityId = entity->id,
                             .operation = operation_,
                             .axis = hit->axis,
                             .initialLocal = *local,
                             .initialWorld = *worldTransform,
                             .worldAxis = worldAxis,
                             .screenDirection =
                                 screenLength > 0.001F
                                     ? runtime::Vec2{screenX / screenLength,
                                                     screenY / screenLength}
                                     : runtime::Vec2{1.0F, 0.0F}};
        return action;
      }
    }
  }

  action.selectionChanged = true;
  action.selectedEntityId =
      pickSceneEntity3D(world, sceneView.camera(), input.mousePosition,
                        input.viewportSize)
          .value_or("");
  return action;
}

} // namespace demi::editor
