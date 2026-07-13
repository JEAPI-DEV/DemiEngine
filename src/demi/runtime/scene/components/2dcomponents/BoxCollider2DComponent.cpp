#include "demi/runtime/scene/components/2dcomponents/BoxCollider2DComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
#include <algorithm>
namespace demi::runtime {
void BoxCollider2DComponent::parse(const nlohmann::json &json, Entity &entity) {
  BoxCollider2DComponent component;
  if (auto value = scene_loading::vec2Field(json, "size"))
    component.size = *value;
  if (auto value = scene_loading::vec2Field(json, "offset"))
    component.offset = *value;
  component.isTrigger =
      scene_loading::boolField(json, "is_trigger").value_or(false);
  component.layer = scene_loading::stringOr(json, "layer");
  component.categoryBits = static_cast<std::uint16_t>(std::clamp(
      static_cast<int>(
          scene_loading::numberField(json, "category_bits").value_or(1.0F)),
      0, 0xFFFF));
  component.maskBits = static_cast<std::uint16_t>(std::clamp(
      static_cast<int>(
          scene_loading::numberField(json, "mask_bits").value_or(65535.0F)),
      0, 0xFFFF));
  component.debugVisible =
      scene_loading::boolField(json, "debug_visible").value_or(true);
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
