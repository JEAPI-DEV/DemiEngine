#pragma once

#include "demi/runtime/concurrency/AsyncCompletion.h"

struct lua_State;

namespace demi::runtime {

struct LuaWorkerContext {
  bool isWorker = false;
  std::stop_token stopToken;
};

// Registry access belongs to the Lua state's owning thread. Coroutines share
// their VM's context. Install for each job and clear before reusing the VM.
[[nodiscard]] LuaWorkerContext luaWorkerContext(lua_State *state);
void setLuaWorkerContext(lua_State *state, std::stop_token stopToken);
void clearLuaWorkerContext(lua_State *state);

// Throws std::runtime_error outside a worker and std::invalid_argument for a
// null state/completion. Cancellation stops this wait, not the operation.
[[nodiscard]] AsyncWaitResult waitLuaWorkerCompletion(
    lua_State *state, std::shared_ptr<AsyncCompletion> completion,
    std::optional<std::chrono::milliseconds> timeout = std::nullopt);

} // namespace demi::runtime
