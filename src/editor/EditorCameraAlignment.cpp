#include "demi/runtime/scene/WorldQueries.h"
#include "editor/EditorWorkspace.h"
#include <cmath>

namespace demi::editor {
namespace {
nlohmann::json vector(runtime::Vec3 value) {
  return {value.x, value.y, value.z};
}
nlohmann::json vector(runtime::Vec2 value) { return {value.x, value.y}; }
} // namespace

bool EditorWorkspace::canAlignSelectedCameraToView() const {
  if (!project_ || activeDocument_ != EditorWorkspaceDocument::Scene ||
      selectedEntityIds_.size() != 1)
    return false;
  const auto *camera = selectedEntity();
  if (!camera)
    return false;
  return viewDimension() == EditorSceneViewDimension::ThreeDimensional
             ? camera->hasComponent<runtime::Camera3DComponent>() &&
                   camera->hasComponent<runtime::Transform3DComponent>()
             : camera->hasComponent<runtime::Camera2DComponent>() &&
                   camera->hasComponent<runtime::Transform2DComponent>();
}

bool EditorWorkspace::alignSelectedCameraToView(std::string &error) {
  error.clear();
  if (!canAlignSelectedCameraToView()) {
    error = "Select one camera with a matching transform in the active scene "
            "or prefab view.";
    return false;
  }
  const auto &entity = *selectedEntity();
  std::vector<EditorFieldEdit> edits;
  const auto add = [&](const char *component, const char *field,
                       nlohmann::json replacement, nlohmann::json current) {
    // Leave unchanged defaults and unrelated camera fields out of authored
    // JSON.
    if (replacement != current)
      edits.push_back(
          {resolveSceneTarget(
               {.entityId = entity.id, .component = component, .field = field}),
           std::move(replacement)});
  };
  if (viewDimension() == EditorSceneViewDimension::ThreeDimensional) {
    const auto view = sceneView_.camera();
    const auto world =
        runtime::resolveWorldTransform3D(project_->world, entity);
    if (!world) {
      error = "The camera hierarchy cannot be resolved.";
      return false;
    }
    auto desired = *world;
    desired.position = view.position;
    desired.rotation = runtime::lookAtRotation3D({}, view.forward);
    const auto local =
        runtime::worldToLocalTransform3D(project_->world, entity, desired);
    if (!local) {
      error = "The camera parent transform is missing or has a zero scale.";
      return false;
    }
    const auto &transform = *entity.component<runtime::Transform3DComponent>();
    const auto &camera = *entity.component<runtime::Camera3DComponent>();
    add("Transform3D", "position", vector(local->position),
        vector(transform.position));
    add("Transform3D", "rotation", vector(local->rotation),
        vector(transform.rotation));
    add("Camera3D", "target_offset", {0, 0, view.focusDistance},
        vector(camera.targetOffset));
    add("Camera3D", "up_axis", 1, camera.upAxis);
    add("Camera3D", "perspective", view.projection.perspective,
        camera.perspective);
    if (view.projection.perspective)
      add("Camera3D", "fov", view.projection.fov, camera.fov);
    else
      add("Camera3D", "orthographic_size", view.projection.orthographicSize,
          camera.orthographicSize);
    add("Camera3D", "near_clip", view.projection.nearClip, camera.nearClip);
    add("Camera3D", "far_clip", view.projection.farClip, camera.farClip);
  } else {
    const auto view = sceneView2D_.camera();
    const auto &transform = *entity.component<runtime::Transform2DComponent>();
    auto position = view.position;
    if (!transform.parent.empty()) {
      const auto *parent =
          runtime::findEntity(project_->world, transform.parent);
      if (!parent || !parent->hasComponent<runtime::Transform2DComponent>()) {
        error = "The camera parent transform cannot be resolved.";
        return false;
      }
      const auto scale = runtime::worldScale2D(project_->world, *parent);
      if (std::abs(scale.x) <= 0.000001F || std::abs(scale.y) <= 0.000001F) {
        error = "The camera parent has a zero scale.";
        return false;
      }
      const auto origin = runtime::worldPosition2D(project_->world, *parent);
      position = runtime::rotate2D(
          {position.x - origin.x, position.y - origin.y},
          -runtime::worldRotation2D(project_->world, *parent));
      position.x /= scale.x;
      position.y /= scale.y;
    }
    add("Transform2D", "position", vector(position),
        vector(transform.position));
    add("Camera2D", "orthographic_size", view.projection.orthographicSize,
        entity.component<runtime::Camera2DComponent>()->orthographicSize);
  }
  if (edits.empty())
    return true;
  return mutateAndRebuild(
      [&](EditorSceneDocument &document, std::string &failure) {
        return document.setFieldValues(std::move(edits), failure);
      },
      error);
}
} // namespace demi::editor
