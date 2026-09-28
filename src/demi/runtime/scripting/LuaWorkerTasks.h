#pragma once

#include <cstddef>
#include <memory>
#include <string>

struct lua_State;

namespace demi::runtime {

enum class LuaWorkerTaskStatus {
  Queued,
  Running,
  Cancelling,
  Completed,
  Failed,
  Cancelled,
};

const char *luaWorkerTaskStatusName(LuaWorkerTaskStatus status);

// Handles contain no borrowed Lua state or host pointers. Terminal state is
// published only after worker execution and cleanup have finished.
class LuaWorkerTask {
public:
  ~LuaWorkerTask();

  [[nodiscard]] bool done() const;
  [[nodiscard]] LuaWorkerTaskStatus status() const;
  [[nodiscard]] std::string error() const;
  // Queued work settles immediately; running work requests cooperative stop.
  bool cancel();
  // Called on the receiving Lua thread; returns the number of pushed values.
  // Pending, failed and cancelled tasks push nil followed by an error string.
  int pushResult(lua_State *state) const;

private:
  struct State;
  explicit LuaWorkerTask(std::shared_ptr<State> state);
  std::shared_ptr<State> state_;
  friend class LuaWorkerTasks;
};

class LuaWorkerTasks {
public:
  LuaWorkerTasks();
  ~LuaWorkerTasks();
  LuaWorkerTasks(const LuaWorkerTasks &) = delete;
  LuaWorkerTasks &operator=(const LuaWorkerTasks &) = delete;

  // Attach/fork/configure are called on the host Lua thread. No pool or worker
  // VM is created until the first fork. The default pool has two workers.
  void attach(lua_State *state);
  void configure(std::size_t workerCount);
  [[nodiscard]] std::shared_ptr<LuaWorkerTask>
  fork(lua_State *state, int functionIndex, int firstArg, int argCount);
  void cancelAll();
  // Requests cancellation, joins workers and invalidates host bindings.
  void shutdown();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  std::shared_ptr<void> bindingLifetime_;
  friend void installLuaTaskBindings(lua_State *, LuaWorkerTasks &);
};

void installLuaTaskBindings(lua_State *state, LuaWorkerTasks &tasks);

} // namespace demi::runtime
