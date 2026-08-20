#include "editor/EditorSceneViewState.h"

#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <cmath>
#include <ranges>

namespace demi::editor {
namespace {

constexpr float HalfPi = 1.57079632679F;

float length(const runtime::Vec3 value) {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

runtime::Vec3 normalized(const runtime::Vec3 value,
                         const runtime::Vec3 fallback) {
  const float magnitude = length(value);
  if (magnitude <= 0.00001F)
    return fallback;
  return {value.x / magnitude, value.y / magnitude, value.z / magnitude};
}

runtime::Vec3 add(const runtime::Vec3 left, const runtime::Vec3 right) {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

runtime::Vec3 multiply(const runtime::Vec3 value, const float scalar) {
  return {value.x * scalar, value.y * scalar, value.z * scalar};
}

} // namespace

void EditorSceneViewState::reset(const runtime::World &world) {
  cameraSettings_ = {};
  cameraSettings_.clearColor = {0.055F, 0.07F, 0.09F, 1.0F};
  cameraSettings_.renderHud = false;
  focus_ = {};
  position_ = {6.0F, 4.0F, 6.0F};
  distance_ = length(position_);
  forward_ = normalized(multiply(position_, -1.0F), {0.0F, 0.0F, 1.0F});
  yaw_ = std::atan2(forward_.x, forward_.z);
  pitch_ = std::asin(std::clamp(forward_.y, -1.0F, 1.0F));
  projection_ = EditorProjection::Perspective;
  transformSpace_ = EditorTransformSpace::Local;
  capturesPointer_ = false;
  updateOrientation();
  (void)alignToFirstCamera(world);
}

bool EditorSceneViewState::alignToFirstCamera(const runtime::World &world) {
  const auto found =
      std::ranges::find_if(world.entities, [](const auto &entity) {
        return entity.enabled &&
               entity.template hasComponent<runtime::Camera3DComponent>();
      });
  if (found == world.entities.end())
    return false;
  const auto transform = runtime::resolveWorldTransform3D(world, *found);
  if (!transform)
    return false;

  cameraSettings_ = *found->component<runtime::Camera3DComponent>();
  cameraSettings_.clearColor = {0.055F, 0.07F, 0.09F, 1.0F};
  cameraSettings_.clearMode = "color";
  cameraSettings_.debugMode = "shaded";
  cameraSettings_.renderMask.clear();
  cameraSettings_.renderTarget.clear();
  cameraSettings_.renderHud = false;
  position_ = transform->position;
  forward_ = normalized(
      runtime::transformDirection3D(*transform, cameraSettings_.targetOffset),
      runtime::forwardDirection3D(*transform));
  distance_ = std::max(length(cameraSettings_.targetOffset), 5.0F);
  focus_ = add(position_, multiply(forward_, distance_));
  yaw_ = std::atan2(forward_.x, forward_.z);
  pitch_ = std::asin(std::clamp(forward_.y, -1.0F, 1.0F));
  projection_ = cameraSettings_.perspective ? EditorProjection::Perspective
                                            : EditorProjection::Orthographic;
  updateOrientation();
  return true;
}

void EditorSceneViewState::update(const EditorViewportInput &input) {
  const bool canBegin = input.hovered && input.focused;
  const bool orbiting = input.orbitButton && input.orbitModifier;
  if (!capturesPointer_ && canBegin &&
      (orbiting || input.panButton || input.flyButton))
    capturesPointer_ = true;
  if (capturesPointer_ && !orbiting && !input.panButton && !input.flyButton)
    capturesPointer_ = false;

  const bool acceptsInput = canBegin || capturesPointer_;
  if (!acceptsInput)
    return;

  if ((orbiting || input.flyButton) &&
      (input.mouseDelta.x != 0.0F || input.mouseDelta.y != 0.0F)) {
    yaw_ -= input.mouseDelta.x * 0.005F;
    pitch_ = std::clamp(pitch_ - input.mouseDelta.y * 0.005F, -HalfPi + 0.01F,
                        HalfPi - 0.01F);
    updateOrientation();
  }

  const runtime::Vec3 right =
      normalized({forward_.z, 0.0F, -forward_.x}, {1.0F, 0.0F, 0.0F});
  if (input.panButton) {
    const float scale = std::max(distance_, 1.0F) * 0.0015F;
    focus_ = add(focus_, multiply(right, -input.mouseDelta.x * scale));
    focus_ = add(focus_, multiply(up_, input.mouseDelta.y * scale));
    position_ = add(focus_, multiply(forward_, -distance_));
  }

  if (input.wheel != 0.0F) {
    if (projection_ == EditorProjection::Perspective) {
      distance_ = std::clamp(distance_ * std::exp(-input.wheel * 0.16F), 0.05F,
                             10000.0F);
      position_ = add(focus_, multiply(forward_, -distance_));
    } else {
      cameraSettings_.orthographicSize = std::clamp(
          cameraSettings_.orthographicSize * std::exp(-input.wheel * 0.16F),
          0.01F, 10000.0F);
    }
  }

  if (input.flyButton) {
    const float dt = std::clamp(input.deltaSeconds, 0.0F, 0.1F);
    const float speed = (input.fast ? 20.0F : 5.0F) * dt;
    runtime::Vec3 movement{};
    if (input.moveForward)
      movement = add(movement, forward_);
    if (input.moveBackward)
      movement = add(movement, multiply(forward_, -1.0F));
    if (input.moveRight)
      movement = add(movement, right);
    if (input.moveLeft)
      movement = add(movement, multiply(right, -1.0F));
    if (input.moveUp)
      movement.y += 1.0F;
    if (input.moveDown)
      movement.y -= 1.0F;
    if (length(movement) > 0.00001F) {
      movement = multiply(normalized(movement, {}), speed);
      position_ = add(position_, movement);
      focus_ = add(focus_, movement);
    }
  }
}

bool EditorSceneViewState::frameEntity(const runtime::World &world,
                                       const std::string_view entityId) {
  const auto found =
      std::ranges::find(world.entities, entityId, &runtime::Entity::id);
  if (found == world.entities.end())
    return false;
  const auto transform = runtime::resolveWorldTransform3D(world, *found);
  if (!transform)
    return false;
  focus_ = transform->position;
  const float radius =
      std::max({std::abs(transform->scale.x), std::abs(transform->scale.y),
                std::abs(transform->scale.z), 0.5F});
  distance_ = std::max(radius * 3.0F, 2.0F);
  cameraSettings_.orthographicSize = std::max(radius * 1.5F, 1.0F);
  position_ = add(focus_, multiply(forward_, -distance_));
  return true;
}

EditorSceneViewCamera EditorSceneViewState::camera() const {
  runtime::Camera3DComponent projection = cameraSettings_;
  projection.perspective = projection_ == EditorProjection::Perspective;
  projection.renderTarget.clear();
  projection.renderHud = false;
  return {.projection = std::move(projection),
          .position = position_,
          .forward = forward_,
          .up = up_,
          .debugGeometry = {.forceColliders = showColliders,
                            .bounds = showBounds,
                            .lights = showLights,
                            .cameras = showCameras}};
}

void EditorSceneViewState::setProjection(const EditorProjection projection) {
  projection_ = projection;
}

void EditorSceneViewState::updateOrientation() {
  const float cosPitch = std::cos(pitch_);
  forward_ = normalized(
      {std::sin(yaw_) * cosPitch, std::sin(pitch_), std::cos(yaw_) * cosPitch},
      {0.0F, 0.0F, 1.0F});
  const runtime::Vec3 right =
      normalized({forward_.z, 0.0F, -forward_.x}, {1.0F, 0.0F, 0.0F});
  up_ = normalized({forward_.y * right.z - forward_.z * right.y,
                    forward_.z * right.x - forward_.x * right.z,
                    forward_.x * right.y - forward_.y * right.x},
                   {0.0F, 1.0F, 0.0F});
  position_ = add(focus_, multiply(forward_, -distance_));
}

} // namespace demi::editor
