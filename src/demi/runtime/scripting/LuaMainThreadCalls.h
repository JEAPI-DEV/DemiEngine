#pragma once

#include <memory>

struct lua_State;

namespace demi::runtime {

class LuaMainThreadCalls {
public:
  LuaMainThreadCalls();
  ~LuaMainThreadCalls();
  LuaMainThreadCalls(const LuaMainThreadCalls &) = delete;
  LuaMainThreadCalls &operator=(const LuaMainThreadCalls &) = delete;

  // Lifecycle/dispatch belong to the owning main thread. Shutdown before
  // closing its Lua state; keep this owner alive until workers have joined.
  void attach(lua_State *state);
  // Worker only: function at 1, copied arguments at 2+. Returns copied
  // multireturns, or nil/error. Cancellation cannot undo an executing callback.
  int call(lua_State *worker);
  // Runs one queue snapshot without holding locks across Lua execution.
  void dispatch();
  // Invalidates queued work, including the current dispatch snapshot. Running
  // callbacks finish normally. New submissions use the new generation.
  void cancelPending();
  void shutdown();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace demi::runtime
