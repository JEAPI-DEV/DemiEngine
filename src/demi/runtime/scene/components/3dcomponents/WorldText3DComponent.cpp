#include "demi/runtime/scene/components/3dcomponents/WorldText3DComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>

namespace demi::runtime {
void WorldText3DComponent::parse(const nlohmann::json &json, Entity &entity) {
  WorldText3DComponent component;
  component.text = scene_loading::stringOr(json, "text");
  component.font = scene_loading::stringOr(json, "font");
  if (auto value = scene_loading::colorField(json, "color"))
    component.color = *value;
  component.fontSize = std::max(
      scene_loading::numberField(json, "font_size").value_or(1.0F), 0.01F);
  component.billboard =
      scene_loading::boolField(json, "billboard").value_or(true);
  component.maxDistance = std::max(
      scene_loading::numberField(json, "max_distance").value_or(100.0F),
      0.0F);
  component.renderMask = scene_loading::stringOr(json, "render_mask");
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
