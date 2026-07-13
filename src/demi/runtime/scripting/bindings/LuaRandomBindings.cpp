#include "demi/runtime/scripting/bindings/LuaRandomBindings.h"

#include <sol/sol.hpp>

#include <string>

namespace demi::runtime {

void LuaRandomBindingModule::install(LuaScriptHost &host,
                                     lua_State *state) const {
  sol::table random = sol::state_view(state).create_named_table("Random");
  random.set_function(
      "seed", [&host](const std::uint64_t seed) { host.seedRandom(seed); });
  random.set_function("state",
                      [&host] { return std::to_string(host.randomState()); });
  random.set_function("restore", [&host](const std::string &stateValue) {
    try {
      host.restoreRandomState(std::stoull(stateValue));
      return true;
    } catch (...) {
      return false;
    }
  });
  random.set_function("value", [&host] { return host.randomValue(); });
  random.set_function("range",
                      [&host](const float minimum, const float maximum) {
                        return host.randomRange(minimum, maximum);
                      });
  random.set_function("integer", [&host](const int minimum, const int maximum) {
    return host.randomInteger(minimum, maximum);
  });
}

} // namespace demi::runtime
