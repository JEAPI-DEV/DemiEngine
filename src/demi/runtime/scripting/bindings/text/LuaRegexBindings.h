#pragma once

#include "demi/runtime/scripting/bindings/LuaBindingModule.h"

namespace demi::runtime {

class LuaRegexBindingModule final : public LuaBindingModule {
public:
  void install(LuaScriptHost &host, lua_State *state) const override;
};

} // namespace demi::runtime
