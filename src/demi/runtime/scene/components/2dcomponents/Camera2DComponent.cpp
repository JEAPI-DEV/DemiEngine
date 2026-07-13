#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
#include <algorithm>
namespace demi::runtime {
void Camera2DComponent::parse(const nlohmann::json &json, Entity &entity) {
  Camera2DComponent component;
  if (auto value = scene_loading::colorField(json, "clear_color"))
    component.clearColor = *value;
  if (auto value = scene_loading::numberField(json, "orthographic_size"))
    component.orthographicSize = *value;
  component.target = scene_loading::stringOr(json, "target");
  component.followSpeed = std::max(
      scene_loading::numberField(json, "follow_speed").value_or(0.0F), 0.0F);
  if (const auto value = scene_loading::vec2Field(json, "follow_offset"))
    component.followOffset = *value;
  const auto boundsMin = scene_loading::vec2Field(json, "bounds_min");
  const auto boundsMax = scene_loading::vec2Field(json, "bounds_max");
  if (boundsMin && boundsMax) {
    component.boundsMin = *boundsMin;
    component.boundsMax = *boundsMax;
    component.hasBounds = true;
  }
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
