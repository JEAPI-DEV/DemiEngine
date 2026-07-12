#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void Transform2DComponent::parse(const nlohmann::json &json, Entity &entity) {
  Transform2DComponent component;
  component.parent = scene_loading::stringOr(json, "parent");
  if (auto value = scene_loading::vec2Field(json, "position"))
    component.position = *value;
  if (auto value = scene_loading::numberField(json, "rotation"))
    component.rotation = *value;
  if (auto value = scene_loading::vec2Field(json, "scale"))
    component.scale = *value;
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
