#pragma once

#include "demi/runtime/scripting/bindings/LuaBindingModule.h"

#include <filesystem>

namespace demi::runtime {

class DatabaseService;

// The service is borrowed and must outlive this Lua state. Join Lua workers
// before shutting down the service or its owning host.
void installLuaDatabaseBindings(lua_State *state, DatabaseService &service,
                                std::filesystem::path userDataPath);

class LuaDatabaseBindingModule final : public LuaBindingModule {
public:
  void install(LuaScriptHost &host, lua_State *state) const override;
};

} // namespace demi::runtime
