#pragma once

#include "demi/runtime/scripting/bindings/LuaBindingModule.h"

namespace demi::runtime {

class TcpClient;

// Call on the VM's owning thread. The client must outlive calls into this VM.
void installLuaTcpBindings(lua_State *state, TcpClient &client);

class LuaTcpBindingModule final : public LuaBindingModule {
public:
  void install(LuaScriptHost &host, lua_State *state) const override;
};

} // namespace demi::runtime
