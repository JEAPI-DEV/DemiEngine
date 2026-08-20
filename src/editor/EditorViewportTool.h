#pragma once

#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "editor/EditorSceneJson.h"
#include "editor/EditorSceneViewState.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace demi::runtime {
struct World;
}

namespace demi::editor {

enum class EditorGizmoOperation { Translate, Rotate, Scale };
enum class EditorGizmoAxis { X, Y, Z };
enum class EditorDragCompletion { None, Finish, Cancel };

struct EditorViewportToolInput {
  runtime::Vec2 mousePosition;
  runtime::Vec2 mouseDelta;
  runtime::Vec2 viewportSize;
  bool hovered = false;
  bool focused = false;
  bool leftPressed = false;
  bool leftDown = false;
  bool leftReleased = false;
  bool navigationModifier = false;
  bool cancelPressed = false;
};

struct EditorGizmoLine {
  EditorGizmoAxis axis = EditorGizmoAxis::X;
  runtime::Vec2 start;
  runtime::Vec2 end;
};

struct EditorGizmoPresentation {
  runtime::Vec2 origin;
  std::vector<EditorGizmoLine> axes;
};

struct EditorViewportEdit {
  SceneValueTarget target;
  nlohmann::json value;
};

struct EditorViewportToolAction {
  bool selectionChanged = false;
  std::string selectedEntityId;
  std::optional<EditorViewportEdit> edit;
  EditorDragCompletion completion = EditorDragCompletion::None;
};

// UI-free gizmo presentation and transform-drag state. Presentation supplies
// only pointer facts and renders the returned lines; authored mutation remains
// in EditorWorkspace.
class EditorViewportTool {
public:
  [[nodiscard]] EditorViewportToolAction
  update(const runtime::World &world, std::string_view selectedEntityId,
         const EditorSceneViewState &sceneView,
         const EditorViewportToolInput &input);
  [[nodiscard]] EditorGizmoPresentation
  presentation(const runtime::World &world, std::string_view selectedEntityId,
               const EditorSceneViewState &sceneView,
               runtime::Vec2 viewportSize) const;

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
    runtime::Transform3DComponent initialLocal;
    runtime::WorldTransform3D initialWorld;
    runtime::Vec3 worldAxis;
    runtime::Vec2 screenDirection;
    float pixels = 0.0F;
  };
  std::optional<ActiveDrag> active_;
  EditorGizmoOperation operation_ = EditorGizmoOperation::Translate;
};

} // namespace demi::editor
