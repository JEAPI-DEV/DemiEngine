#pragma once

#include "demi/runtime/scripting/LuaTransfer.h"

#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <string>

struct lua_State;

namespace demi::runtime {

// Owns only native transfer graphs, never references into a Lua VM. Values
// captured for storage must reject shared handles to prevent ownership cycles.
// Native callers must hold mutex when accessing values. Recursive acquisition
// of one map is safe; acquire different maps in a consistent global order.
// Immutable graph references can be retained and marshalled after unlocking.
class LuaSharedMap {
public:
  std::recursive_timed_mutex mutex;
  std::map<std::string, std::shared_ptr<const LuaTransferGraph>> values;
};

// Main VMs never block. Worker VMs use timed waits with cancellation checks.
void installLuaSharedBindings(lua_State *L, bool worker = false);
// The caller keeps cancellation alive until the VM is closed or reset to null.
void setLuaSharedCancellation(lua_State *L, const std::atomic<bool> *cancel);
std::shared_ptr<LuaSharedMap> luaSharedMap(lua_State *L, int index);
void pushLuaSharedMap(lua_State *L, std::shared_ptr<LuaSharedMap> map);

} // namespace demi::runtime
