#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void SpriteComponent::parse(const nlohmann::json &json, Entity &entity) {
  SpriteComponent component;
  component.texture = scene_loading::stringOr(json, "texture");
  component.shape = scene_loading::stringOr(json, "shape", "rectangle");
  component.layer = scene_loading::stringOr(json, "layer");
  component.sortingOrder = static_cast<int>(
      scene_loading::numberField(json, "sorting_order").value_or(0.0F));
  if (auto value = scene_loading::vec2Field(json, "source_position"))
    component.sourcePosition = *value;
  if (auto value = scene_loading::vec2Field(json, "source_size"))
    component.sourceSize = *value;
  if (auto value = scene_loading::vec2Field(json, "size"))
    component.size = *value;
  if (auto value = scene_loading::vec2Field(json, "pivot"))
    component.pivot = *value;
  component.flipX = scene_loading::boolField(json, "flip_x").value_or(false);
  component.flipY = scene_loading::boolField(json, "flip_y").value_or(false);
  if (auto value = scene_loading::colorField(json, "color"))
    component.color = *value;
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
