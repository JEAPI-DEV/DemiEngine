#include "demi/runtime/scene/components/3dcomponents/ConvexCollider3DComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

namespace demi::runtime {

void ConvexCollider3DComponent::parse(const nlohmann::json &json,
                                      Entity &entity) {
  ConvexCollider3DComponent component;
  component.points = scene_loading::vec3ArrayField(json, "points");
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
