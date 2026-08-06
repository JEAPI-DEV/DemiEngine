#include "demi/runtime/scene/components/2dcomponents/Joint2DComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>

namespace demi::runtime {

void Joint2DComponent::parse(const nlohmann::json &json, Entity &entity) {
  Joint2DComponent component;
  component.type = scene_loading::stringOr(json, "type", "revolute");
  component.otherEntity = scene_loading::stringOr(json, "other_entity");
  if (const auto value = scene_loading::vec2Field(json, "anchor"))
    component.anchor = *value;
  if (const auto value = scene_loading::vec2Field(json, "other_anchor"))
    component.otherAnchor = *value;
  if (const auto value = scene_loading::vec2Field(json, "axis"))
    component.axis = *value;
  component.lowerLimit =
      scene_loading::numberField(json, "lower_limit").value_or(0.0F);
  component.upperLimit =
      scene_loading::numberField(json, "upper_limit").value_or(0.0F);
  component.enableLimit =
      scene_loading::boolField(json, "enable_limit").value_or(false);
  component.motorSpeed =
      scene_loading::numberField(json, "motor_speed").value_or(0.0F);
  component.maxMotorForce = std::max(
      scene_loading::numberField(json, "max_motor_force").value_or(0.0F), 0.0F);
  component.maxMotorTorque = std::max(
      scene_loading::numberField(json, "max_motor_torque").value_or(0.0F),
      0.0F);
  component.enableMotor =
      scene_loading::boolField(json, "enable_motor").value_or(false);
  component.maxLength = std::max(
      scene_loading::numberField(json, "max_length").value_or(1.0F), 0.0F);
  component.correctionFactor = std::clamp(
      scene_loading::numberField(json, "correction_factor").value_or(0.3F),
      0.0F, 1.0F);
  component.collideConnected =
      scene_loading::boolField(json, "collide_connected").value_or(false);
  entity.setComponent(std::move(component));
}

} // namespace demi::runtime
