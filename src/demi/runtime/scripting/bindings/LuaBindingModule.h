#pragma once

#include "demi/runtime/scripting/LuaScriptHost.h"

extern "C" {
#include <lua.h>
}

namespace demi::runtime {

class LuaBindingModule {
public:
  virtual ~LuaBindingModule() = default;
  virtual void install(LuaScriptHost& host, lua_State* state) const = 0;
};

} // namespace demi::runtime
