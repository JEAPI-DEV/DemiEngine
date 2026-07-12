#include "demi/runtime/scene/components/3dcomponents/SphereCollider3DComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void SphereCollider3DComponent::parse(const nlohmann::json &json,
                                      Entity &entity) {
  SphereCollider3DComponent component;
  if (auto value = scene_loading::numberField(json, "radius"))
    component.radius = *value;
  if (auto value = scene_loading::vec3Field(json, "offset"))
    component.offset = *value;
  component.isTrigger =
      scene_loading::boolField(json, "is_trigger").value_or(false);
  component.layer = scene_loading::stringOr(json, "layer");
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
