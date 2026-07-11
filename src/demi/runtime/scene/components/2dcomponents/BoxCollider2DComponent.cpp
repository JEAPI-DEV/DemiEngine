#include "demi/runtime/scene/components/2dcomponents/BoxCollider2DComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
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
  entity.boxCollider2D = component;
}
} // namespace demi::runtime
