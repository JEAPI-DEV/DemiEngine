#include "demi/runtime/scene/components/3dcomponents/CapsuleCollider3DComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>

namespace demi::runtime {

void CapsuleCollider3DComponent::parse(const nlohmann::json &json,
                                       Entity &entity) {
  CapsuleCollider3DComponent component;
  if (auto value = scene_loading::numberField(json, "radius"))
    component.radius = std::max(*value, 0.001F);
  if (auto value = scene_loading::numberField(json, "height"))
    component.height = std::max(*value, component.radius * 2.0F);
  if (auto value = scene_loading::vec3Field(json, "offset"))
    component.offset = *value;
  component.isTrigger =
      scene_loading::boolField(json, "is_trigger").value_or(false);
  component.layer = scene_loading::stringOr(json, "layer");
  component.debugVisible =
      scene_loading::boolField(json, "debug_visible").value_or(false);
  entity.setComponent(std::move(component));
}

} // namespace demi::runtime
