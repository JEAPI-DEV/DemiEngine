#include "demi/runtime/scene/components/2dcomponents/DistanceJoint2DComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>

namespace demi::runtime {

void DistanceJoint2DComponent::parse(const nlohmann::json &json,
                                     Entity &entity) {
  DistanceJoint2DComponent component;
  component.otherEntity = scene_loading::stringOr(json, "other_entity");
  if (const auto value = scene_loading::vec2Field(json, "anchor"))
    component.anchor = *value;
  if (const auto value = scene_loading::vec2Field(json, "other_anchor"))
    component.otherAnchor = *value;
  component.length = std::max(
      scene_loading::numberField(json, "length").value_or(1.0F), 0.001F);
  component.stiffness = std::max(
      scene_loading::numberField(json, "stiffness").value_or(0.0F), 0.0F);
  component.damping = std::max(
      scene_loading::numberField(json, "damping").value_or(0.0F), 0.0F);
  component.collideConnected =
      scene_loading::boolField(json, "collide_connected").value_or(false);
  entity.setComponent(std::move(component));
}

} // namespace demi::runtime
