#include "demi/runtime/scripting/bindings/persistence/LuaSaveBindings.h"
#include "demi/runtime/scripting/bindings/LuaBindingHelpers.h"
#include "demi/runtime/scripting/bindings/LuaJsonBridge.h"
#include "demi/runtime/scripting/persistence/GameSaveDocument.h"

#include <nlohmann/json.hpp>
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
  save.set_function("write_state", [&host](const std::string &slot,
                                           const sol::table state,
                                           sol::optional<sol::table> options) {
    int version = persistence::CurrentGameSaveVersion;
    bool autosave = false;
    int sequence = 0;
    std::string reason;
    if (options) {
      version = options->get_or("format_version", version);
      autosave = options->get_or("autosave", false);
      sequence = options->get_or("sequence", 0);
      reason = options->get_or("reason", std::string{});
    }
    return host.writeGameSaveDocument(slot, luaObjectToJson(state).dump(),
                                      version, autosave, sequence, reason);
  });
  save.set_function("read_state", [state, &host](const std::string &slot) {
    if (!host.readGameSaveDocument(slot))
      return sol::make_object(state, sol::nil);
    return luaReadSaveTable(state, host, slot);
  });
  save.set_function("metadata", [state, &host](const std::string &slot) {
    const auto text = host.readGameSaveDocument(slot);
    if (!text)
      return sol::make_object(state, sol::nil);
    try {
      const auto document = nlohmann::json::parse(*text);
      return jsonToLuaObject(state, document.at("metadata"));
    } catch (...) {
      return sol::make_object(state, sol::nil);
    }
  });
  save.set_function("last_error", [&host]() { return host.lastSaveError(); });
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
