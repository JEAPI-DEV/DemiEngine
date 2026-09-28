#pragma once

#include "demi/runtime/scripting/bindings/LuaBindingModule.h"

namespace demi::runtime {

class HttpClient;

// Call on the VM's owning thread. The client must outlive calls into this VM.
void installLuaHttpBindings(lua_State *state, HttpClient &client);

class LuaHttpBindingModule final : public LuaBindingModule {
public:
  void install(LuaScriptHost &host, lua_State *state) const override;
};

} // namespace demi::runtime
