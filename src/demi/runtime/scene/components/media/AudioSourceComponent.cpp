#include "demi/runtime/scene/components/media/AudioSourceComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void AudioSourceComponent::parse(const nlohmann::json &json, Entity &entity) {
  AudioSourceComponent component;
  component.clip = scene_loading::stringOr(json, "clip");
  component.playOnStart =
      scene_loading::boolField(json, "play_on_start").value_or(false);
  component.loop = scene_loading::boolField(json, "loop").value_or(false);
  if (auto value = scene_loading::numberField(json, "volume"))
    component.volume = *value;
  entity.audioSource = component;
}
} // namespace demi::runtime
