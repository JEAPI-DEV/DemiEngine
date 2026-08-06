#pragma once

#include "demi/runtime/physics/Physics3DTypes.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"

#include <optional>

namespace demi::runtime {

struct Camera3DComponent;

[[nodiscard]] CameraRay3D
cameraScreenRay3D(const WorldTransform3D &transform,
                  const Camera3DComponent &camera, Vec2 screen,
                  Vec2 viewport);

[[nodiscard]] std::optional<Vec2>
worldToScreen3D(const WorldTransform3D &transform,
                const Camera3DComponent &camera, Vec3 world,
                Vec2 viewport);

[[nodiscard]] Vec3
screenToWorld3D(const WorldTransform3D &transform,
                const Camera3DComponent &camera, Vec2 screen, Vec2 viewport,
                float distance);

} // namespace demi::runtime
