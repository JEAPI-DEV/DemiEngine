#include "demi/runtime/scripting/bindings/persistence/LuaSaveBindings.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"
#include <sol/sol.hpp>
namespace demi::runtime {
void LuaSaveBindingModule::install(LuaScriptHost &host,
                                   lua_State *state) const {
  sol::table save = sol::state_view(state).create_named_table("Save");
  save.set_function("get_number", [&host](const std::string &slot,
                                          const std::string &key,
                                          sol::optional<float> fallback) {
    return host.saveNumber(slot, key).value_or(fallback.value_or(0.0F));
  });
  save.set_function("set_number", [&host](const std::string &slot,
                                          const std::string &key, float value) {
    return host.setSaveNumber(slot, key, value);
  });
  save.set_function("get_string", [&host](const std::string &slot,
                                          const std::string &key,
                                          sol::optional<std::string> fallback) {
    return host.saveString(slot, key).value_or(fallback.value_or(""));
  });
  save.set_function("set_string",
                    [&host](const std::string &slot, const std::string &key,
                            const std::string &value) {
                      return host.setSaveString(slot, key, value);
                    });
  save.set_function("read", [state, &host](const std::string &slot) {
    return luaReadSaveTable(state, host, slot);
  });
  save.set_function("write",
                    [&host](const std::string &slot, const sol::table table,
                            sol::optional<int> version) {
                      return luaWriteSaveTable(host, slot, table, version);
                    });
  save.set_function("exists", [&host](const std::string &slot) {
    return host.saveExists(slot);
  });
  save.set_function("delete", [&host](const std::string &slot) {
    return host.deleteSave(slot);
  });
  save.set_function("version", [&host](const std::string &slot) {
    return host.saveFormatVersion(slot);
  });
  save.set_function(
      "register_migration",
      [state, &host](int from, int to, const sol::function callback) {
        return luaRegisterSaveMigration(state, host, from, to, callback);
      });
}
} // namespace demi::runtime
