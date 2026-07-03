#pragma once

#include "demi/runtime/scripting/bindings/LuaBindingModule.h"

namespace demi::runtime {

#if DEMI_HAS_LUA54
class LuaNetworkBindingModule final : public LuaBindingModule {
public:
  void install(LuaScriptHost& host, lua_State* state) const override;
};
#endif

} // namespace demi::runtime
