#pragma once

#include "demi/runtime/render/bgfx3d/DebugGeometry3D.h"
#include "demi/runtime/scene/components/3dcomponents/Camera3DComponent.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string_view>

namespace demi::runtime {
struct World;
}

namespace demi::editor {

enum class EditorProjection { Perspective, Orthographic };
enum class EditorTransformSpace { Local, World };

struct EditorViewportInput {
  float deltaSeconds = 0.0F;
  runtime::Vec2 mousePosition;
  runtime::Vec2 mouseDelta;
  float wheel = 0.0F;
  bool hovered = false;
  bool focused = false;
  bool orbitButton = false;
  bool panButton = false;
  bool flyButton = false;
  bool orbitModifier = false;
  bool moveForward = false;
  bool moveBackward = false;
  bool moveLeft = false;
  bool moveRight = false;
  bool moveUp = false;
  bool moveDown = false;
  bool fast = false;
};

struct EditorSceneViewCamera {
  runtime::Camera3DComponent projection;
  runtime::Vec3 position;
  runtime::Vec3 forward{0.0F, 0.0F, 1.0F};
  runtime::Vec3 up{0.0F, 1.0F, 0.0F};
  runtime::render::DebugGeometry3DRequest debugGeometry;
};

// Owns transient scene-view navigation. Nothing in this type is serialized
// into an authored scene.
class EditorSceneViewState {
public:
  void reset(const runtime::World &world);
  void update(const EditorViewportInput &input);
  [[nodiscard]] bool frameEntity(const runtime::World &world,
                                 std::string_view entityId);
  [[nodiscard]] bool alignToFirstCamera(const runtime::World &world);

  [[nodiscard]] EditorSceneViewCamera camera() const;
  [[nodiscard]] bool capturesPointer() const { return capturesPointer_; }

  [[nodiscard]] EditorProjection projection() const { return projection_; }
  void setProjection(EditorProjection projection);
  [[nodiscard]] EditorTransformSpace transformSpace() const {
    return transformSpace_;
  }
  void setTransformSpace(EditorTransformSpace space) {
    transformSpace_ = space;
  }

  float translationSnap = 1.0F;
  float rotationSnapDegrees = 15.0F;
  float scaleSnap = 0.1F;
  bool showBounds = false;
  bool showColliders = false;
  bool showLights = true;
  bool showCameras = true;

private:
  void updateOrientation();

  runtime::Camera3DComponent cameraSettings_;
  runtime::Vec3 focus_{};
  runtime::Vec3 position_{6.0F, 4.0F, 6.0F};
  runtime::Vec3 forward_{-0.666667F, -0.333333F, -0.666667F};
  runtime::Vec3 up_{0.0F, 1.0F, 0.0F};
  float yaw_ = -2.35619449F;
  float pitch_ = -0.33983691F;
  float distance_ = 9.0F;
  EditorProjection projection_ = EditorProjection::Perspective;
  EditorTransformSpace transformSpace_ = EditorTransformSpace::Local;
  bool capturesPointer_ = false;
};

} // namespace demi::editor
