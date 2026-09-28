#pragma once

#include "demi/runtime/scripting/LuaTransfer.h"

#include <string_view>

namespace demi::runtime {

struct LuaTaskFunction {
  struct Upvalue {
    int index;
    bool environment;
    LuaTransferGraph value;
  };
  std::string bytecode;
  std::vector<Upvalue> upvalues;
  LuaTransferGraph arguments;
};

// Preserves the source stack. Only primitive/SharedMap captures are accepted;
// arguments are copied graphs. A custom source _ENV is rejected.
[[nodiscard]] LuaTaskFunction
captureLuaTaskFunction(lua_State *state, int functionIndex, int firstArg,
                       int argCount, std::string_view operation = "Task.fork");

// Appends multireturns, preserving the stack on failure. Throws on Lua
// errors. environmentIndex=0 selects destination globals; otherwise use the
// supplied table. Invocation is protected, including loading/restoring values.
int invokeLuaTaskFunction(lua_State *state, const LuaTaskFunction &function,
                          int environmentIndex = 0);

} // namespace demi::runtime
