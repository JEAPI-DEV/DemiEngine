#pragma once

#include "demi/runtime/physics/Physics3DTypes.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"

#include <optional>

namespace demi::runtime {

struct Camera3DComponent;

struct CameraProjectedPoint3D {
  Vec2 screen;
  float depth;
  float pixelsPerUnit;
};

// Backend-neutral right-handed view. Perspective halfHeight is measured at
// unit depth; orthographic halfHeight is half the authored full visible height.
struct CameraProjection3D {
  Vec3 position;
  Vec3 forward;
  Vec3 right;
  Vec3 up;
  Vec2 viewport;
  float halfHeight;
  float nearClip;
  float farClip;
  bool perspective;

  [[nodiscard]] float pixelsPerUnit(float depth = 1.0F) const;
  [[nodiscard]] std::optional<CameraProjectedPoint3D> project(Vec3 world) const;
  [[nodiscard]] CameraRay3D ray(Vec2 screen) const;
};

// Explicit frame poses preserve reflected, shadow, and editor override views.
// Degenerate view directions/up vectors do not define a projection.
[[nodiscard]] std::optional<CameraProjection3D>
cameraProjection3D(const Camera3DComponent &camera, Vec3 position, Vec3 forward,
                   Vec3 up, Vec2 viewport);
[[nodiscard]] std::optional<CameraProjection3D>
cameraProjection3D(const WorldTransform3D &transform,
                   const Camera3DComponent &camera, Vec2 viewport);

[[nodiscard]] CameraRay3D cameraScreenRay3D(const WorldTransform3D &transform,
                                            const Camera3DComponent &camera,
                                            Vec2 screen, Vec2 viewport);

[[nodiscard]] std::optional<Vec2>
worldToScreen3D(const WorldTransform3D &transform,
                const Camera3DComponent &camera, Vec3 world, Vec2 viewport);

[[nodiscard]] Vec3 screenToWorld3D(const WorldTransform3D &transform,
                                   const Camera3DComponent &camera, Vec2 screen,
                                   Vec2 viewport, float distance);

} // namespace demi::runtime
