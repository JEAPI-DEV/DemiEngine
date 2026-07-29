#include "demi/runtime/scene/components/media/AudioSourceComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
#include <algorithm>
namespace demi::runtime {
namespace {
AudioSpatialMode parseSpatialMode(const std::string &value) {
  if (value == "2d")
    return AudioSpatialMode::TwoDimensional;
  if (value == "3d")
    return AudioSpatialMode::ThreeDimensional;
  return AudioSpatialMode::None;
}
AudioAttenuation parseAttenuation(const std::string &value) {
  if (value == "none")
    return AudioAttenuation::None;
  if (value == "linear")
    return AudioAttenuation::Linear;
  if (value == "exponential")
    return AudioAttenuation::Exponential;
  return AudioAttenuation::Inverse;
}
AudioVoiceStealing parseVoiceStealing(const std::string &value) {
  if (value == "reject")
    return AudioVoiceStealing::Reject;
  if (value == "quietest")
    return AudioVoiceStealing::Quietest;
  return AudioVoiceStealing::Oldest;
}
} // namespace
void AudioSourceComponent::parse(const nlohmann::json &json, Entity &entity) {
  AudioSourceComponent component;
  component.clip = scene_loading::stringOr(json, "clip");
  component.playOnStart =
      scene_loading::boolField(json, "play_on_start").value_or(false);
  component.loop = scene_loading::boolField(json, "loop").value_or(false);
  component.streaming =
      scene_loading::boolField(json, "streaming").value_or(false);
  component.bus = scene_loading::stringOr(json, "bus", "sfx");
  if (auto value = scene_loading::numberField(json, "volume"))
    component.volume = std::max(*value, 0.0F);
  component.pitch = std::max(
      scene_loading::numberField(json, "pitch").value_or(1.0F), 0.01F);
  component.pan = std::clamp(
      scene_loading::numberField(json, "pan").value_or(0.0F), -1.0F, 1.0F);
  component.spatialMode =
      parseSpatialMode(scene_loading::stringOr(json, "spatial"));
  component.attenuation =
      parseAttenuation(
          scene_loading::stringOr(json, "attenuation", "inverse"));
  component.minDistance = std::max(
      scene_loading::numberField(json, "min_distance").value_or(1.0F), 0.001F);
  component.maxDistance = std::max(
      scene_loading::numberField(json, "max_distance").value_or(100.0F),
      component.minDistance);
  component.rolloff = std::max(
      scene_loading::numberField(json, "rolloff").value_or(1.0F), 0.0F);
  component.doppler =
      scene_loading::boolField(json, "doppler").value_or(false);
  component.fadeIn = std::max(
      scene_loading::numberField(json, "fade_in").value_or(0.0F), 0.0F);
  component.concurrencyGroup =
      scene_loading::stringOr(json, "concurrency_group");
  component.maxVoices = static_cast<std::uint32_t>(std::max(
      scene_loading::numberField(json, "max_voices").value_or(0.0F), 0.0F));
  component.voiceStealing =
      parseVoiceStealing(
          scene_loading::stringOr(json, "voice_stealing", "oldest"));
  component.pauseWithGame =
      scene_loading::boolField(json, "pause_with_game").value_or(true);
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
