#include "demi/runtime/scene/components/gameplay/HitboxControllerComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void HitboxControllerComponent::parse(const nlohmann::json &json,
                                      Entity &entity) {
  HitboxControllerComponent component;
  if (auto value = scene_loading::vec2Field(json, "hurtbox"))
    component.hurtbox = *value;
  component.script = scene_loading::stringOr(json, "script");
  entity.hitboxController = component;
}
} // namespace demi::runtime
