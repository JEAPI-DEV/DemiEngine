#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void SpriteComponent::parse(const nlohmann::json &json, Entity &entity) {
  SpriteComponent component;
  component.texture = scene_loading::stringOr(json, "texture");
  component.shape = scene_loading::stringOr(json, "shape", "rectangle");
  component.layer = scene_loading::stringOr(json, "layer");
  if (auto value = scene_loading::colorField(json, "color"))
    component.color = *value;
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
