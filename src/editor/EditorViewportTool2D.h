#pragma once

#include "editor/EditorSceneView2DState.h"
#include "editor/EditorViewportTool.h"

#include "demi/runtime/scene/components/2dcomponents/IsoTransformComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"

#include <optional>

namespace demi::editor {

// Owns 2D picking and transform-drag state. Like the 3D tool, it only emits
// authored field edits; EditorWorkspace owns validation, undo, and rebuilding.
class EditorViewportTool2D {
public:
  [[nodiscard]] EditorViewportToolAction
  update(const runtime::World &world, std::string_view selectedEntityId,
         const EditorSceneView2DState &sceneView,
         const EditorViewportToolInput &input,
         const std::optional<EditorIsoGridCell> &selectedGridCell = {});
  [[nodiscard]] EditorGizmoPresentation presentation(
      const runtime::World &world, std::string_view selectedEntityId,
      const EditorSceneView2DState &sceneView, runtime::Vec2 viewportSize,
      const std::optional<EditorIsoGridCell> &selectedGridCell = {}) const;

  void cancelDrag() { active_.reset(); }
  [[nodiscard]] bool isDragging() const { return active_.has_value(); }
  [[nodiscard]] EditorGizmoOperation operation() const { return operation_; }
  void setOperation(EditorGizmoOperation operation) {
    if (!active_)
      operation_ = operation;
  }

private:
  struct ActiveDrag {
    std::string entityId;
    EditorGizmoOperation operation = EditorGizmoOperation::Translate;
    EditorGizmoAxis axis = EditorGizmoAxis::X;
    bool isIsometric = false;
    bool isGridCell = false;
    EditorIsoGridCell gridCell;
    nlohmann::json initialCellTextures;
    runtime::Transform2DComponent initialLocal;
    runtime::Vec2 initialIsoTile;
    runtime::Vec2 initialWorldPosition;
    runtime::Vec2 initialWorldScale{1.0F, 1.0F};
    float initialWorldRotation = 0.0F;
    runtime::Vec2 worldAxis;
    runtime::Vec2 screenDirection;
    float pixelsPerStep = 1.0F;
    float pixels = 0.0F;
  };

  std::optional<ActiveDrag> active_;
  EditorGizmoOperation operation_ = EditorGizmoOperation::Translate;
};

} // namespace demi::editor
