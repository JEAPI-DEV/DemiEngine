#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
#include <algorithm>
namespace demi::runtime {
void AnimationPlayer3DComponent::parse(const nlohmann::json &json,
                                       Entity &entity) {
  AnimationPlayer3DComponent component;
  if (auto value = scene_loading::numberField(json, "clip"))
    component.clip = std::max(0, static_cast<int>(*value));
  if (auto value = scene_loading::numberField(json, "speed"))
    component.speed = std::max(0.0F, *value);
  if (auto value = scene_loading::numberField(json, "time"))
    component.time = std::max(0.0F, *value);
  component.loop = scene_loading::boolField(json, "loop").value_or(true);
  component.playing = scene_loading::boolField(json, "playing").value_or(true);
  entity.animationPlayer3D = component;
}
} // namespace demi::runtime
