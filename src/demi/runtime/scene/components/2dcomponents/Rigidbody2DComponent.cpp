#include "demi/runtime/scene/components/2dcomponents/Rigidbody2DComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
#include <algorithm>
namespace demi::runtime {
void Rigidbody2DComponent::parse(const nlohmann::json &json, Entity &entity) {
  Rigidbody2DComponent component;
  component.bodyType = scene_loading::stringOr(json, "body_type", "dynamic");
  if (auto value = scene_loading::vec2Field(json, "velocity"))
    component.velocity = *value;
  if (auto value = scene_loading::numberField(json, "gravity_scale"))
    component.gravityScale = *value;
  if (auto value = scene_loading::numberField(json, "bounciness"))
    component.bounciness = *value;
  component.lockRotation =
      scene_loading::boolField(json, "lock_rotation").value_or(true);
  component.angularVelocity =
      scene_loading::numberField(json, "angular_velocity").value_or(0.0F);
  component.linearDamping = std::max(
      scene_loading::numberField(json, "linear_damping").value_or(0.0F), 0.0F);
  component.angularDamping = std::max(
      scene_loading::numberField(json, "angular_damping").value_or(0.0F), 0.0F);
  component.continuous =
      scene_loading::boolField(json, "continuous").value_or(false);
  component.allowSleep =
      scene_loading::boolField(json, "allow_sleep").value_or(true);
  component.awake = scene_loading::boolField(json, "awake").value_or(true);
  component.bodyEnabled =
      scene_loading::boolField(json, "body_enabled").value_or(true);
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
