#include "demi/runtime/scripting/bindings/media/LuaAudioBindings.h"
#include <sol/sol.hpp>
namespace demi::runtime {
void LuaAudioBindingModule::install(LuaScriptHost &host,
                                    lua_State *state) const {
  sol::state_view lua(state);
  sol::table audio = lua.create_named_table("Audio");
  audio.set_function("play", [&host](const std::string &assetId) {
    return host.playAudio(assetId);
  });
  audio.set_function(
      "stop", [&host](std::uint64_t handle) { return host.stopAudio(handle); });
  audio.set_function("set_master_volume",
                     [&host](float volume) { host.setMasterVolume(volume); });
  audio.set_function("get_master_volume",
                     [&host] { return host.masterVolume(); });
  sol::table source = lua.create_named_table("AudioSource");
  source.set_function("play", [&host](const std::string &entityId) {
    return host.playAudioSource(entityId);
  });
  source.set_function("stop", [&host](const std::string &entityId) {
    return host.stopAudioSource(entityId);
  });
}
} // namespace demi::runtime
