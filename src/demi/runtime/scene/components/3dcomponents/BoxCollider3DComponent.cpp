#include "demi/runtime/scene/components/3dcomponents/BoxCollider3DComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void BoxCollider3DComponent::parse(const nlohmann::json &json, Entity &entity) {
  BoxCollider3DComponent component;
  if (auto value = scene_loading::vec3Field(json, "size"))
    component.size = *value;
  if (auto value = scene_loading::vec3Field(json, "offset"))
    component.offset = *value;
  component.isTrigger =
      scene_loading::boolField(json, "is_trigger").value_or(false);
  component.layer = scene_loading::stringOr(json, "layer");
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
