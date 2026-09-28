#pragma once

#include "demi/runtime/scripting/bindings/LuaBindingModule.h"

namespace demi::runtime {

// Pure data conversion facilities; does not access the live asset store.
void installLuaDataValueBindings(lua_State *state);

class LuaDataBindingModule final : public LuaBindingModule {
public:
  void install(LuaScriptHost &host, lua_State *state) const override;
};

} // namespace demi::runtime
