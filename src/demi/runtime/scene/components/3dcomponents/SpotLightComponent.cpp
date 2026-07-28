#include "demi/runtime/scene/components/3dcomponents/SpotLightComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>

namespace demi::runtime {
void SpotLightComponent::parse(const nlohmann::json &json, Entity &entity) {
  SpotLightComponent component;
  if (auto value = scene_loading::colorField(json, "color"))
    component.color = *value;
  component.intensity = std::max(
      scene_loading::numberField(json, "intensity").value_or(1.0F), 0.0F);
  component.range =
      std::max(scene_loading::numberField(json, "range").value_or(12.0F),
               0.001F);
  component.innerAngle = std::clamp(
      scene_loading::numberField(json, "inner_angle").value_or(25.0F), 0.0F,
      179.0F);
  component.outerAngle = std::clamp(
      scene_loading::numberField(json, "outer_angle").value_or(40.0F),
      component.innerAngle, 179.0F);
  if (auto value = scene_loading::vec3Field(json, "direction"))
    component.direction = *value;
  component.castsShadows =
      scene_loading::boolField(json, "casts_shadows").value_or(false);
  component.renderMask = scene_loading::stringOr(json, "render_mask");
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
