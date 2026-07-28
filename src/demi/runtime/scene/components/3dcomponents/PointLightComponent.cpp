#include "demi/runtime/scene/components/3dcomponents/PointLightComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>

namespace demi::runtime {
void PointLightComponent::parse(const nlohmann::json &json, Entity &entity) {
  PointLightComponent component;
  if (auto value = scene_loading::colorField(json, "color"))
    component.color = *value;
  component.intensity = std::max(
      scene_loading::numberField(json, "intensity").value_or(1.0F), 0.0F);
  component.range =
      std::max(scene_loading::numberField(json, "range").value_or(8.0F),
               0.001F);
  component.castsShadows =
      scene_loading::boolField(json, "casts_shadows").value_or(false);
  component.renderMask = scene_loading::stringOr(json, "render_mask");
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
