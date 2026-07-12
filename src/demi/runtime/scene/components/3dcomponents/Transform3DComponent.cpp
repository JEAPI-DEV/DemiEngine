#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void Transform3DComponent::parse(const nlohmann::json &json, Entity &entity) {
  Transform3DComponent component;
  component.parent = scene_loading::stringOr(json, "parent");
  if (auto value = scene_loading::vec3Field(json, "position"))
    component.position = *value;
  if (auto value = scene_loading::vec3Field(json, "rotation"))
    component.rotation = *value;
  if (auto value = scene_loading::vec3Field(json, "scale"))
    component.scale = *value;
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
