#include "demi/runtime/scripting/bindings/media/LuaCutsceneBindings.h"
#include <sol/sol.hpp>
namespace demi::runtime {
void LuaCutsceneBindingModule::install(LuaScriptHost &host,
                                       lua_State *state) const {
  sol::table cutscene = sol::state_view(state).create_named_table("Cutscene");
  cutscene.set_function("play", [&host](const std::string &id) {
    return host.startCutscene(id);
  });
  cutscene.set_function("pause", [&host] { return host.pauseCutscene(); });
  cutscene.set_function("resume", [&host] { return host.resumeCutscene(); });
  cutscene.set_function("skip", [&host] { return host.stopCutscene(); });
  cutscene.set_function("stop", [&host] { return host.stopCutscene(); });
  cutscene.set_function("is_playing",
                        [&host] { return host.isCutscenePlaying(); });
  cutscene.set_function("active", [&host] { return host.activeCutscene(); });
}
} // namespace demi::runtime
