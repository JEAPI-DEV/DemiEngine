#pragma once

#include "demi/runtime/scripting/bindings/LuaBindingModule.h"

namespace demi::runtime {

void installLuaVectorMathBindings(lua_State *state);

class LuaVectorMathBindingModule final : public LuaBindingModule {
public:
  void install(LuaScriptHost &host, lua_State *state) const override;
};

} // namespace demi::runtime
