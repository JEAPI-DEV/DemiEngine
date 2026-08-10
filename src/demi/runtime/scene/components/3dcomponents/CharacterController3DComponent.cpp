#include "demi/runtime/scene/components/3dcomponents/CharacterController3DComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>

namespace demi::runtime {

void CharacterController3DComponent::parse(const nlohmann::json &json,
                                           Entity &entity) {
  CharacterController3DComponent component;
  if (auto value = scene_loading::numberField(json, "step_height"))
    component.stepHeight = std::max(*value, 0.0F);
  if (auto value = scene_loading::numberField(json, "slope_limit"))
    component.slopeLimit = std::clamp(*value, 0.0F, 89.9F);
  if (auto value = scene_loading::numberField(json, "skin_width"))
    component.skinWidth = std::max(*value, 0.0001F);
  if (auto value = scene_loading::numberField(json, "gravity"))
    component.gravity = *value;
  entity.setComponent(std::move(component));
}

} // namespace demi::runtime
