#pragma once

#include "demi/runtime/scripting/LuaScriptHost.h"

#if DEMI_HAS_LUA54
extern "C" {
#include <lua.h>
}
#endif

namespace demi::runtime {

#if DEMI_HAS_LUA54
class LuaBindingModule {
public:
  virtual ~LuaBindingModule() = default;
  virtual void install(LuaScriptHost& host, lua_State* state) const = 0;
};
#endif

} // namespace demi::runtime
