#include "demi/runtime/scene/components/3dcomponents/Rigidbody3DComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
#include <algorithm>
namespace demi::runtime {
void Rigidbody3DComponent::parse(const nlohmann::json &json, Entity &entity) {
  Rigidbody3DComponent component;
  component.bodyType = scene_loading::stringOr(json, "body_type", "dynamic");
  if (auto value = scene_loading::vec3Field(json, "velocity"))
    component.velocity = *value;
  if (auto value = scene_loading::vec3Field(json, "angular_velocity"))
    component.angularVelocity = *value;
  component.useGravity =
      scene_loading::boolField(json, "use_gravity").value_or(true);
  if (auto value = scene_loading::numberField(json, "gravity_scale"))
    component.gravityScale = *value;
  if (auto value = scene_loading::numberField(json, "mass"))
    component.mass = std::max(*value, 0.0001F);
  if (auto value = scene_loading::numberField(json, "linear_damping"))
    component.linearDamping = std::max(*value, 0.0F);
  if (auto value = scene_loading::numberField(json, "angular_damping"))
    component.angularDamping = std::max(*value, 0.0F);
  if (auto value = scene_loading::numberField(json, "friction"))
    component.friction = std::max(*value, 0.0F);
  if (auto value = scene_loading::numberField(json, "restitution"))
    component.restitution = std::clamp(*value, 0.0F, 1.0F);
  component.continuous =
      scene_loading::boolField(json, "continuous").value_or(false);
  component.allowSleep =
      scene_loading::boolField(json, "allow_sleep").value_or(true);
  component.awake = scene_loading::boolField(json, "awake").value_or(true);
  component.bodyEnabled =
      scene_loading::boolField(json, "body_enabled").value_or(true);
  component.interpolate =
      scene_loading::boolField(json, "interpolate").value_or(true);
  component.lockPositionX =
      scene_loading::boolField(json, "lock_position_x").value_or(false);
  component.lockPositionY =
      scene_loading::boolField(json, "lock_position_y").value_or(false);
  component.lockPositionZ =
      scene_loading::boolField(json, "lock_position_z").value_or(false);
  component.lockRotationX =
      scene_loading::boolField(json, "lock_rotation_x").value_or(false);
  component.lockRotationY =
      scene_loading::boolField(json, "lock_rotation_y").value_or(false);
  component.lockRotationZ =
      scene_loading::boolField(json, "lock_rotation_z").value_or(false);
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
