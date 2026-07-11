#include "demi/runtime/scene/components/3dcomponents/Rigidbody3DComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void Rigidbody3DComponent::parse(const nlohmann::json &json, Entity &entity) {
  Rigidbody3DComponent component;
  component.bodyType = scene_loading::stringOr(json, "body_type", "dynamic");
  if (auto value = scene_loading::vec3Field(json, "velocity"))
    component.velocity = *value;
  component.useGravity =
      scene_loading::boolField(json, "use_gravity").value_or(true);
  if (auto value = scene_loading::numberField(json, "gravity_scale"))
    component.gravityScale = *value;
  entity.rigidbody3D = component;
}
} // namespace demi::runtime
