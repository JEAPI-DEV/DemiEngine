#include "demi/runtime/scene/components/2dcomponents/Rigidbody2DComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void Rigidbody2DComponent::parse(const nlohmann::json &json, Entity &entity) {
  Rigidbody2DComponent component;
  component.bodyType = scene_loading::stringOr(json, "body_type", "dynamic");
  if (auto value = scene_loading::vec2Field(json, "velocity"))
    component.velocity = *value;
  if (auto value = scene_loading::numberField(json, "gravity_scale"))
    component.gravityScale = *value;
  if (auto value = scene_loading::numberField(json, "bounciness"))
    component.bounciness = *value;
  component.lockRotation =
      scene_loading::boolField(json, "lock_rotation").value_or(true);
  entity.rigidbody2D = component;
}
} // namespace demi::runtime
