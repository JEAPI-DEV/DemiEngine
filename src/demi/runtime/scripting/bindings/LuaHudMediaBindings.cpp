#include "demi/runtime/scripting/bindings/LuaHudMediaBindings.h"

#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"

#include <sol/sol.hpp>

namespace demi::runtime {

void LuaHudMediaBindingModule::install(LuaScriptHost& host, lua_State* state) const {
  sol::state_view lua(state);

  sol::table hud = lua.create_named_table("Hud");
  hud.set_function("text", [&host](const std::string& id, const std::string& text, float x, float y, sol::optional<float> scale, sol::optional<float> r, sol::optional<float> g, sol::optional<float> b, sol::optional<float> a) { return host.createHudText(id, text, x, y, scale.value_or(3.0F), Color{r.value_or(1.0F), g.value_or(1.0F), b.value_or(1.0F), a.value_or(1.0F)}); });
  hud.set_function("rect", [&host](const std::string& id, float x, float y, float width, float height, sol::optional<float> r, sol::optional<float> g, sol::optional<float> b, sol::optional<float> a) { return host.createHudRect(id, x, y, width, height, Color{r.value_or(1.0F), g.value_or(1.0F), b.value_or(1.0F), a.value_or(1.0F)}); });
  hud.set_function("set_text", [&host](const std::string& id, const std::string& text) { return host.setHudText(id, text); });
  hud.set_function("set_text_scale", [&host](const std::string& id, float scale) { return host.setHudTextScale(id, scale); });
  hud.set_function("set_button_label", [&host](const std::string& id, const std::string& label) { return host.setHudButtonLabel(id, label); });
  hud.set_function("set_rect", [&host](const std::string& id, float x, float y, float width, float height) { return host.setHudRect(id, x, y, width, height); });
  hud.set_function("set_color", [&host](const std::string& id, float r, float g, float b, sol::optional<float> a) { return host.setHudColor(id, Color{r, g, b, a.value_or(1.0F)}); });
  hud.set_function("set_visible", [&host](const std::string& id, bool visible) { return host.setHudVisible(id, visible); });
  hud.set_function("set_group_visible", [&host](const std::string& group, bool visible) { return host.setHudGroupVisible(group, visible); });
  hud.set_function("get_text", [&host](const std::string& id) { return host.hudText(id); });

  sol::table save = lua.create_named_table("Save");
  save.set_function("get_number", [&host](const std::string& slot, const std::string& key, sol::optional<float> fallback) { return host.saveNumber(slot, key).value_or(fallback.value_or(0.0F)); });
  save.set_function("set_number", [&host](const std::string& slot, const std::string& key, float value) { return host.setSaveNumber(slot, key, value); });
  save.set_function("get_string", [&host](const std::string& slot, const std::string& key, sol::optional<std::string> fallback) { return host.saveString(slot, key).value_or(fallback.value_or("")); });
  save.set_function("set_string", [&host](const std::string& slot, const std::string& key, const std::string& value) { return host.setSaveString(slot, key, value); });
  save.set_function("read", [state, &host](const std::string& slot) { return luaReadSaveTable(state, host, slot); });
  save.set_function("write", [&host](const std::string& slot, const sol::table table, sol::optional<int> version) { return luaWriteSaveTable(host, slot, table, version); });
  save.set_function("exists", [&host](const std::string& slot) { return host.saveExists(slot); });
  save.set_function("delete", [&host](const std::string& slot) { return host.deleteSave(slot); });
  save.set_function("version", [&host](const std::string& slot) { return host.saveFormatVersion(slot); });
  save.set_function("register_migration", [state, &host](int fromVersion, int toVersion, const sol::function callback) { return luaRegisterSaveMigration(state, host, fromVersion, toVersion, callback); });

  sol::table audio = lua.create_named_table("Audio");
  audio.set_function("play", [&host](const std::string& assetId) { return host.playAudio(assetId); });
  audio.set_function("stop", [&host](std::uint64_t handle) { return host.stopAudio(handle); });
  audio.set_function("set_master_volume", [&host](float volume) { host.setMasterVolume(volume); });
  audio.set_function("get_master_volume", [&host] { return host.masterVolume(); });

  sol::table audioSource = lua.create_named_table("AudioSource");
  audioSource.set_function("play", [&host](const std::string& entityId) { return host.playAudioSource(entityId); });
  audioSource.set_function("stop", [&host](const std::string& entityId) { return host.stopAudioSource(entityId); });

  sol::table video = lua.create_named_table("Video");
  video.set_function("play", [&host](const std::string& assetId, sol::optional<bool> loop) { return host.playVideo(assetId, loop.value_or(false)); });
  video.set_function("play_component", [&host](const std::string& entityId) { return host.playVideoPlayer(entityId); });
  video.set_function("stop", [&host](std::uint64_t handle) { return host.stopVideo(handle); });
  video.set_function("is_playing", [&host](std::uint64_t handle) { return host.isVideoPlaying(handle); });

  sol::table cutscene = lua.create_named_table("Cutscene");
  cutscene.set_function("play", [&host](const std::string& id) { return host.startCutscene(id); });
  cutscene.set_function("pause", [&host] { return host.pauseCutscene(); });
  cutscene.set_function("resume", [&host] { return host.resumeCutscene(); });
  cutscene.set_function("skip", [&host] { return host.stopCutscene(); });
  cutscene.set_function("stop", [&host] { return host.stopCutscene(); });
  cutscene.set_function("is_playing", [&host] { return host.isCutscenePlaying(); });
  cutscene.set_function("active", [&host] { return host.activeCutscene(); });
}

} // namespace demi::runtime
