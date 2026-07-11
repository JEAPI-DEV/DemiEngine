#include "demi/runtime/scripting/bindings/media/LuaVideoBindings.h"
#include <sol/sol.hpp>
namespace demi::runtime {
void LuaVideoBindingModule::install(LuaScriptHost &host,
                                    lua_State *state) const {
  sol::table video = sol::state_view(state).create_named_table("Video");
  video.set_function(
      "play", [&host](const std::string &assetId, sol::optional<bool> loop) {
        return host.playVideo(assetId, loop.value_or(false));
      });
  video.set_function("play_component", [&host](const std::string &entityId) {
    return host.playVideoPlayer(entityId);
  });
  video.set_function(
      "stop", [&host](std::uint64_t handle) { return host.stopVideo(handle); });
  video.set_function("is_playing", [&host](std::uint64_t handle) {
    return host.isVideoPlaying(handle);
  });
}
} // namespace demi::runtime
