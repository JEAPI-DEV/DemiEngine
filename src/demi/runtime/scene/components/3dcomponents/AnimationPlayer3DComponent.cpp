#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
#include <algorithm>
namespace demi::runtime {
void AnimationPlayer3DComponent::parse(const nlohmann::json &json,
                                       Entity &entity) {
  AnimationPlayer3DComponent component;
  component.clipName = scene_loading::stringOr(json, "clip_name");
  if (auto value = scene_loading::numberField(json, "speed"))
    component.speed = std::max(0.0F, *value);
  if (auto value = scene_loading::numberField(json, "time"))
    component.time = std::max(0.0F, *value);
  component.loop =
      scene_loading::boolField(json, "loop").value_or(component.loop);
  component.playing =
      scene_loading::boolField(json, "playing").value_or(component.playing);
  component.visualUpdateRate = std::clamp(
      scene_loading::numberField(json, "visual_update_rate").value_or(0.0F),
      0.0F, 240.0F);
  component.visualUpdateDistance = std::max(
      scene_loading::numberField(json, "visual_update_distance").value_or(0.0F),
      0.0F);
  entity.setComponent(std::move(component));
}
nlohmann::json AnimationPlayer3DComponent::defaults() {
  const AnimationPlayer3DComponent value;
  return {{"clip_name", value.clipName},
          {"speed", value.speed},
          {"time", value.time},
          {"loop", value.loop},
          {"playing", value.playing},
          {"visual_update_rate", value.visualUpdateRate},
          {"visual_update_distance", value.visualUpdateDistance}};
}
} // namespace demi::runtime
