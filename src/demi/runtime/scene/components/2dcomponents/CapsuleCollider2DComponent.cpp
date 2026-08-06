#include "demi/runtime/scene/components/2dcomponents/CapsuleCollider2DComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>

namespace demi::runtime {

void CapsuleCollider2DComponent::parse(const nlohmann::json &json,
                                       Entity &entity) {
  CapsuleCollider2DComponent component;
  if (const auto value = scene_loading::vec2Field(json, "size"))
    component.size = {std::max(value->x, 0.002F), std::max(value->y, 0.002F)};
  if (const auto value = scene_loading::vec2Field(json, "offset"))
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
  component.friction = std::max(
      scene_loading::numberField(json, "friction").value_or(0.2F), 0.0F);
  component.restitution =
      std::clamp(scene_loading::numberField(json, "restitution").value_or(0.0F),
                 0.0F, 1.0F);
  component.density = std::max(
      scene_loading::numberField(json, "density").value_or(1.0F), 0.0F);
  component.debugVisible =
      scene_loading::boolField(json, "debug_visible").value_or(true);
  entity.setComponent(std::move(component));
}

} // namespace demi::runtime
