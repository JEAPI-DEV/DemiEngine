#include "demi/runtime/scripting/bindings/media/LuaAudioBindings.h"
#include "demi/runtime/audio/AudioTypes.h"
#include <sol/sol.hpp>
#include <unordered_map>
namespace demi::runtime {
namespace {

AudioSpatialMode spatialMode(const std::string &value) {
  if (value == "2d")
    return AudioSpatialMode::TwoDimensional;
  if (value == "3d")
    return AudioSpatialMode::ThreeDimensional;
  return AudioSpatialMode::None;
}

AudioAttenuation attenuation(const std::string &value) {
  if (value == "none")
    return AudioAttenuation::None;
  if (value == "linear")
    return AudioAttenuation::Linear;
  if (value == "exponential")
    return AudioAttenuation::Exponential;
  return AudioAttenuation::Inverse;
}

AudioVoiceStealing voiceStealing(const std::string &value) {
  if (value == "reject")
    return AudioVoiceStealing::Reject;
  if (value == "quietest")
    return AudioVoiceStealing::Quietest;
  return AudioVoiceStealing::Oldest;
}

AudioPlaybackRequest requestFrom(const std::string &assetId,
                                 const sol::table &options) {
  AudioPlaybackRequest request{.assetId = assetId,
                               .concurrencyGroup = {},
                               .position = {},
                               .velocity = {}};
  request.bus = options.get_or("bus", std::string("sfx"));
  request.concurrencyGroup =
      options.get_or("concurrency_group", std::string{});
  request.loop = options.get_or("loop", false);
  request.streaming = options.get_or("streaming", false);
  request.volume = options.get_or("volume", 1.0F);
  request.pitch = options.get_or("pitch", 1.0F);
  request.pan = options.get_or("pan", 0.0F);
  request.spatialMode =
      spatialMode(options.get_or("spatial", std::string("none")));
  request.attenuation =
      attenuation(options.get_or("attenuation", std::string("inverse")));
  request.position = {options.get_or("x", 0.0F), options.get_or("y", 0.0F),
                      options.get_or("z", 0.0F)};
  request.minDistance = options.get_or("min_distance", 1.0F);
  request.maxDistance = options.get_or("max_distance", 100.0F);
  request.rolloff = options.get_or("rolloff", 1.0F);
  request.doppler = options.get_or("doppler", false);
  request.delaySeconds = options.get_or("delay", 0.0F);
  request.fadeInSeconds = options.get_or("fade_in", 0.0F);
  request.maxVoices = options.get_or("max_voices", std::uint32_t{0});
  request.voiceStealing =
      voiceStealing(options.get_or("voice_stealing", std::string("oldest")));
  request.pauseWithGame = options.get_or("pause_with_game", true);
  return request;
}

} // namespace
void LuaAudioBindingModule::install(LuaScriptHost &host,
                                    lua_State *state) const {
  sol::state_view lua(state);
  sol::table audio = lua.create_named_table("Audio");
  audio.set_function("play", [&host](const std::string &assetId,
                                     sol::optional<sol::table> options) {
    return options ? host.playAudio(requestFrom(assetId, *options))
                   : host.playAudio(assetId);
  });
  audio.set_function(
      "stop", [&host](std::uint64_t handle) { return host.stopAudio(handle); });
  audio.set_function("set_master_volume",
                     [&host](float volume) { host.setMasterVolume(volume); });
  audio.set_function("get_master_volume",
                     [&host] { return host.masterVolume(); });
  audio.set_function("set_bus_volume",
                     [&host](const std::string &bus, float volume) {
                       return host.setAudioBusVolume(bus, volume);
                     });
  audio.set_function("get_bus_volume", [&host](const std::string &bus) {
    return host.audioBusVolume(bus);
  });
  audio.set_function("set_bus_muted",
                     [&host](const std::string &bus, bool muted) {
                       return host.setAudioBusMuted(bus, muted);
                     });
  audio.set_function("set_bus_paused",
                     [&host](const std::string &bus, bool paused) {
                       return host.setAudioBusPaused(bus, paused);
                     });
  audio.set_function("define_snapshot",
                     [&host](const std::string &name,
                             const sol::table &volumes) {
                       std::unordered_map<std::string, float> values;
                       for (const auto &[key, value] : volumes) {
                         if (key.is<std::string>() && value.is<float>())
                           values[key.as<std::string>()] = value.as<float>();
                       }
                       host.defineAudioSnapshot(name, values);
                     });
  audio.set_function("transition_snapshot",
                     [&host](const std::string &name, float duration) {
                       return host.transitionAudioSnapshot(name, duration);
                     });
  audio.set_function(
      "crossfade",
      [&host](std::uint64_t fromHandle, const std::string &assetId,
              float duration, sol::optional<std::string> bus,
              sol::optional<bool> loop, sol::optional<bool> streaming) {
        return host.crossfadeAudio(fromHandle, assetId,
                                   bus.value_or("music"), duration,
                                   loop.value_or(true),
                                   streaming.value_or(true));
      });
  sol::table source = lua.create_named_table("AudioSource");
  source.set_function("play", [&host](const std::string &entityId) {
    return host.playAudioSource(entityId);
  });
  source.set_function("stop", [&host](const std::string &entityId) {
    return host.stopAudioSource(entityId);
  });
}
} // namespace demi::runtime
