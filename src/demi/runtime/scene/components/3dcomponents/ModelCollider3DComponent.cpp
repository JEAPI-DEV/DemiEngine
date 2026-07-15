#include "demi/runtime/scene/components/3dcomponents/ModelCollider3DComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

namespace demi::runtime {

void ModelCollider3DComponent::parse(const nlohmann::json &json,
                                     Entity &entity) {
  ModelCollider3DComponent component;
  component.asset = scene_loading::stringOr(json, "asset");
  component.isTrigger =
      scene_loading::boolField(json, "is_trigger").value_or(false);
  component.layer = scene_loading::stringOr(json, "layer");
  entity.setComponent(std::move(component));
}

} // namespace demi::runtime
