#pragma once

#include "editor/EditorSceneViewState.h"
#include "editor/EditorSelection.h"

#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"

#include <string_view>

namespace demi::runtime {
struct World;
}

namespace demi::editor {

struct EditorSceneView2DCamera {
  runtime::Camera2DComponent projection;
  runtime::Vec2 position;
};

// Owns transient 2D authoring-camera navigation. It never mutates the
// authored Camera2D or Transform2D components used to seed its initial view.
class EditorSceneView2DState {
public:
  void reset(const runtime::World &world);
  void update(const EditorViewportInput &input);
  [[nodiscard]] bool frameEntity(const runtime::World &world,
                                 std::string_view entityId);
  [[nodiscard]] bool frameGridCell(const runtime::World &world,
                                   const EditorIsoGridCell &cell);
  [[nodiscard]] bool alignToFirstCamera(const runtime::World &world);

  [[nodiscard]] EditorSceneView2DCamera camera() const;
  [[nodiscard]] bool capturesPointer() const { return capturesPointer_; }
  [[nodiscard]] float pixelsPerUnit(float viewportHeight) const;
  [[nodiscard]] EditorTransformSpace transformSpace() const {
    return transformSpace_;
  }
  void setTransformSpace(EditorTransformSpace space) {
    transformSpace_ = space;
  }

  float translationSnap = 1.0F;
  float rotationSnapDegrees = 15.0F;
  float scaleSnap = 0.1F;
  bool showGrid = true;
  bool showBounds = true;
  bool showColliders = false;
  bool showCameras = true;

private:
  runtime::Camera2DComponent cameraSettings_;
  runtime::Vec2 position_;
  EditorTransformSpace transformSpace_ = EditorTransformSpace::Local;
  bool capturesPointer_ = false;
};

} // namespace demi::editor
