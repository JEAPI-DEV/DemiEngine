#include "demi/runtime/scripting/LuaWorkerTasks.h"

#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/scripting/LuaMainThreadCalls.h"
#include "demi/runtime/scripting/LuaServiceModules.h"
#include "demi/runtime/scripting/LuaSharedMap.h"
#include "demi/runtime/scripting/LuaTaskFunction.h"
#include "demi/runtime/scripting/LuaTransfer.h"
#include "demi/runtime/scripting/LuaWorkerContext.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <atomic>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace demi::runtime {
namespace {
bool terminal(LuaWorkerTaskStatus status) {
  return status == LuaWorkerTaskStatus::Completed ||
         status == LuaWorkerTaskStatus::Failed ||
         status == LuaWorkerTaskStatus::Cancelled;
}

char cancellationRegistryKey;
char idleGlobalsRegistryKey;

const std::atomic<bool> *workerCancellation(lua_State *state) {
  lua_rawgetp(state, LUA_REGISTRYINDEX, &cancellationRegistryKey);
  const auto *cancel =
      static_cast<const std::atomic<bool> *>(lua_touserdata(state, -1));
  lua_pop(state, 1);
  return cancel;
}

int cancelled(lua_State *state) {
  const auto *cancel = workerCancellation(state);
  lua_pushboolean(state, cancel && cancel->load(std::memory_order_acquire));
  return 1;
}

int checkCancelled(lua_State *state) {
  const auto *cancel = workerCancellation(state);
  if (cancel && cancel->load(std::memory_order_acquire))
    return luaL_error(state, "cancelled");
  return 0;
}

int requireWorkerModule(lua_State *state) {
  const char *module = luaL_checkstring(state, 1);
  lua_getfield(state, lua_upvalueindex(1), module);
  if (!lua_isfunction(state, -1))
    return luaL_error(state,
                      "Worker module '%s' is unavailable; use Task.main "
                      "for live engine services",
                      module);
  lua_call(state, 0, 1);
  return 1;
}

struct WorkerExecution {
  const LuaTaskFunction &function;
  const std::function<void(lua_State *)> &installModules;
  LuaMainThreadCalls &mainCalls;
};

int mainCall(lua_State *state) {
  auto *calls = static_cast<LuaMainThreadCalls *>(
      lua_touserdata(state, lua_upvalueindex(1)));
  return calls->call(state);
}

// Lua finalizers must not retain a job's environment or write shared data after
// its terminal state is published. Close handlers still run during execution.
int setWorkerMetatable(lua_State *state) {
  luaL_checktype(state, 1, LUA_TTABLE);
  if (!lua_isnil(state, 2)) {
    luaL_checktype(state, 2, LUA_TTABLE);
    lua_pushliteral(state, "__gc");
    lua_rawget(state, 2);
    const bool finalizer = !lua_isnil(state, -1);
    lua_pop(state, 1);
    if (finalizer)
      return luaL_error(state, "Worker table __gc finalizers are unavailable; "
                               "use a close handler for cleanup");
  }
  if (lua_getmetatable(state, 1)) {
    lua_pushliteral(state, "__metatable");
    lua_rawget(state, -2);
    const bool protectedMetatable = !lua_isnil(state, -1);
    lua_pop(state, 2);
    if (protectedMetatable)
      return luaL_error(state, "cannot change a protected metatable");
  }
  lua_settop(state, 2);
  lua_setmetatable(state, 1);
  return 1;
}

void prepareEnvironment(lua_State *state, const WorkerExecution &execution) {
  lua_newtable(state);
  lua_rawseti(state, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
  luaopen_base(state);
  lua_pop(state, 1);
  for (const char *name : {"dofile", "loadfile", "load", "collectgarbage"}) {
    lua_pushnil(state);
    lua_setglobal(state, name);
  }
  lua_pushcfunction(state, setWorkerMetatable);
  lua_setglobal(state, "setmetatable");

  for (const auto &[name, open] :
       {std::pair<const char *, lua_CFunction>{"math", luaopen_math},
        {"string", luaopen_string},
        {"table", luaopen_table},
        {"utf8", luaopen_utf8}}) {
    open(state);
    lua_setglobal(state, name);
  }
  lua_newtable(state);
  lua_setfield(state, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
  lua_newtable(state);
  const luaL_Reg functions[] = {{"cancelled", cancelled},
                                {"check_cancelled", checkCancelled},
                                {nullptr, nullptr}};
  luaL_setfuncs(state, functions, 0);
  lua_pushlightuserdata(state, &execution.mainCalls);
  lua_pushcclosure(state, mainCall, 1);
  lua_setfield(state, -2, "main");
  lua_setglobal(state, "Task");
  installLuaSharedBindings(state, true);
  luaL_getsubtable(state, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
  lua_getfield(state, -1, "demi.shared");
  lua_remove(state, -2);
  lua_call(state, 0, 1);
  lua_setglobal(state, "Shared");
  if (execution.installModules)
    execution.installModules(state);
  publishLuaServiceModules(state);
  luaL_getsubtable(state, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
  lua_pushcclosure(state, requireWorkerModule, 1);
  lua_setglobal(state, "require");
}

int executeWorkerFunction(lua_State *state) {
  const auto *execution = static_cast<const WorkerExecution *>(
      lua_touserdata(state, lua_upvalueindex(1)));
  try {
    prepareEnvironment(state, *execution);
    checkCancelled(state);
    return invokeLuaTaskFunction(state, execution->function);
  } catch (const std::exception &error) {
    lua_pushstring(state, error.what());
  }
  return lua_error(state);
}

int initializeWorkerVM(lua_State *state) {
  installLuaSharedBindings(state, true);
  lua_pushglobaltable(state);
  lua_rawsetp(state, LUA_REGISTRYINDEX, &idleGlobalsRegistryKey);
  return 0;
}

class WorkerVM {
public:
  WorkerVM() : state_(luaL_newstate()) {
    if (!state_)
      throw std::bad_alloc();
    lua_pushcfunction(state_, initializeWorkerVM);
    if (lua_pcall(state_, 0, 0, 0) != LUA_OK) {
      const char *message = lua_tostring(state_, -1);
      const std::runtime_error failure(
          message ? message : "Could not initialize worker VM");
      lua_close(state_);
      throw failure;
    }
  }
  ~WorkerVM() { lua_close(state_); }
  WorkerVM(const WorkerVM &) = delete;
  WorkerVM &operator=(const WorkerVM &) = delete;

  LuaTransferGraph run(const WorkerExecution &execution,
                       const std::atomic<bool> &cancel,
                       std::stop_token stopToken) {
    // This scope ends before the task is settled, including on native errors.
    struct Cleanup {
      lua_State *state;
      ~Cleanup() {
        lua_settop(state, 0);
        setLuaSharedCancellation(state, nullptr);
        clearLuaWorkerContext(state);
        // Drop per-job module closures while the owning services are alive.
        lua_pushnil(state);
        lua_setfield(state, LUA_REGISTRYINDEX, LUA_PRELOAD_TABLE);
        lua_pushnil(state);
        lua_setfield(state, LUA_REGISTRYINDEX, LuaServicesRegistry);
        lua_pushnil(state);
        lua_rawsetp(state, LUA_REGISTRYINDEX, &cancellationRegistryKey);
        lua_rawgetp(state, LUA_REGISTRYINDEX, &idleGlobalsRegistryKey);
        lua_rawseti(state, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
        lua_pushliteral(state, "");
        lua_pushnil(state);
        lua_setmetatable(state, -2);
        lua_pop(state, 1);
      }
    } cleanup{state_};
    setLuaWorkerContext(state_, std::move(stopToken));
    setLuaSharedCancellation(state_, &cancel);
    lua_pushlightuserdata(state_, const_cast<std::atomic<bool> *>(&cancel));
    lua_rawsetp(state_, LUA_REGISTRYINDEX, &cancellationRegistryKey);
    lua_pushlightuserdata(state_, const_cast<WorkerExecution *>(&execution));
    lua_pushcclosure(state_, executeWorkerFunction, 1);
    if (lua_pcall(state_, 0, LUA_MULTRET, 0) != LUA_OK) {
      const char *message = lua_tostring(state_, -1);
      throw std::runtime_error(
          message ? message : "Lua worker raised a non-string error");
    }
    return captureLuaValues(state_, 1, lua_gettop(state_));
  }

private:
  lua_State *state_;
};
} // namespace

struct LuaWorkerTask::State {
  mutable std::mutex mutex;
  std::atomic<bool> cancellation = false;
  std::stop_source stopSource;
  LuaWorkerTaskStatus status = LuaWorkerTaskStatus::Queued;
  std::string error;
  std::shared_ptr<const LuaTransferGraph> results;

  bool cancel() {
    std::scoped_lock lock(mutex);
    if (terminal(status) || status == LuaWorkerTaskStatus::Cancelling)
      return false;
    cancellation.store(true, std::memory_order_release);
    stopSource.request_stop();
    if (status == LuaWorkerTaskStatus::Queued) {
      status = LuaWorkerTaskStatus::Cancelled;
      error = "cancelled";
    } else {
      status = LuaWorkerTaskStatus::Cancelling;
    }
    return true;
  }

  void run(const WorkerExecution &execution) {
    {
      std::scoped_lock lock(mutex);
      if (status != LuaWorkerTaskStatus::Queued)
        return;
      status = LuaWorkerTaskStatus::Running;
    }
    std::shared_ptr<const LuaTransferGraph> output;
    std::string failure;
    try {
      // JobSystem owns these threads. TLS closes each VM when its worker exits.
      thread_local WorkerVM vm;
      output = std::make_shared<LuaTransferGraph>(
          vm.run(execution, cancellation, stopSource.get_token()));
    } catch (const std::exception &exception) {
      failure = exception.what();
    } catch (...) {
      failure = "native Lua worker failure";
    }
    std::scoped_lock lock(mutex);
    if (cancellation.load(std::memory_order_acquire)) {
      status = LuaWorkerTaskStatus::Cancelled;
      error = "cancelled";
    } else if (!output) {
      status = LuaWorkerTaskStatus::Failed;
      error = std::move(failure);
    } else {
      results = std::move(output);
      status = LuaWorkerTaskStatus::Completed;
    }
  }
};

const char *luaWorkerTaskStatusName(LuaWorkerTaskStatus status) {
  switch (status) {
  case LuaWorkerTaskStatus::Queued:
    return "queued";
  case LuaWorkerTaskStatus::Running:
    return "running";
  case LuaWorkerTaskStatus::Cancelling:
    return "cancelling";
  case LuaWorkerTaskStatus::Completed:
    return "completed";
  case LuaWorkerTaskStatus::Failed:
    return "failed";
  case LuaWorkerTaskStatus::Cancelled:
    return "cancelled";
  }
  return "failed";
}

LuaWorkerTask::LuaWorkerTask(std::shared_ptr<State> state)
    : state_(std::move(state)) {}
LuaWorkerTask::~LuaWorkerTask() = default;

bool LuaWorkerTask::done() const {
  std::scoped_lock lock(state_->mutex);
  return terminal(state_->status);
}

LuaWorkerTaskStatus LuaWorkerTask::status() const {
  std::scoped_lock lock(state_->mutex);
  return state_->status;
}

std::string LuaWorkerTask::error() const {
  std::scoped_lock lock(state_->mutex);
  return state_->error;
}

bool LuaWorkerTask::cancel() { return state_->cancel(); }

int LuaWorkerTask::pushResult(lua_State *state) const {
  if (!state || !lua_checkstack(state, 2))
    throw std::invalid_argument(
        "Task results require available Lua stack space");
  std::shared_ptr<const LuaTransferGraph> results;
  std::string failure;
  {
    std::scoped_lock lock(state_->mutex);
    if (state_->status == LuaWorkerTaskStatus::Completed)
      results = state_->results;
    else
      failure = terminal(state_->status) ? state_->error : "pending";
  }
  if (!results) {
    lua_pushnil(state);
    lua_pushlstring(state, failure.data(), failure.size());
    return 2;
  }
  pushLuaValues(state, *results);
  return static_cast<int>(results->roots.size());
}

struct LuaWorkerTasks::Impl {
  struct Active {
    std::mutex mutex;
    std::unordered_map<LuaWorkerTask::State *,
                       std::weak_ptr<LuaWorkerTask::State>>
        entries;
  };
  std::mutex mutex;
  lua_State *main = nullptr;
  std::size_t workerCount = 2;
  std::unique_ptr<JobSystem> jobs;
  std::shared_ptr<Active> active = std::make_shared<Active>();
  std::function<void(lua_State *)> installModules;
  LuaMainThreadCalls mainCalls;
  bool stopping = false;
};

LuaWorkerTasks::LuaWorkerTasks()
    : impl_(std::make_unique<Impl>()),
      bindingLifetime_(std::make_shared<int>(0)) {}
LuaWorkerTasks::~LuaWorkerTasks() { shutdown(); }

void LuaWorkerTasks::attach(lua_State *state) {
  if (!state)
    throw std::invalid_argument("Task worker service requires a Lua state");
  shutdown();
  std::scoped_lock lock(impl_->mutex);
  impl_->main = state;
  impl_->mainCalls.attach(state);
  impl_->stopping = false;
  impl_->active = std::make_shared<Impl::Active>();
  bindingLifetime_ = std::make_shared<int>(0);
}

void LuaWorkerTasks::configure(std::size_t workerCount) {
  if (workerCount == 0)
    throw std::invalid_argument("Task worker count must be positive");
  std::scoped_lock lock(impl_->mutex);
  if (impl_->stopping)
    throw std::runtime_error("Task worker service is shut down");
  if (impl_->jobs)
    throw std::invalid_argument(
        "Task.configure must run before the first fork");
  impl_->workerCount = workerCount;
}

void LuaWorkerTasks::setModuleInstaller(
    std::function<void(lua_State *)> installer) {
  std::scoped_lock lock(impl_->mutex);
  impl_->installModules = std::move(installer);
}

void LuaWorkerTasks::dispatchMainCalls() { impl_->mainCalls.dispatch(); }

std::shared_ptr<LuaWorkerTask> LuaWorkerTasks::fork(lua_State *state,
                                                    int functionIndex,
                                                    int firstArg,
                                                    int argCount) {
  if (!state)
    throw std::invalid_argument("Task.fork requires a Lua state");
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->main || impl_->stopping)
    throw std::runtime_error("Task worker service is unavailable");
  lua_rawgeti(state, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
  const bool attached = lua_tothread(state, -1) == impl_->main;
  lua_pop(state, 1);
  if (!attached)
    throw std::invalid_argument("Task.fork requires the attached Lua VM");

  auto function =
      captureLuaTaskFunction(state, functionIndex, firstArg, argCount);
  auto taskState = std::make_shared<LuaWorkerTask::State>();
  auto task = std::shared_ptr<LuaWorkerTask>(new LuaWorkerTask(taskState));
  if (!impl_->jobs)
    impl_->jobs = std::make_unique<JobSystem>(impl_->workerCount);
  {
    std::scoped_lock activeLock(impl_->active->mutex);
    impl_->active->entries.emplace(taskState.get(), taskState);
  }
  try {
    static_cast<void>(impl_->jobs->submit(
        [taskState, function = std::move(function), registry = impl_->active,
         installer = impl_->installModules, calls = &impl_->mainCalls] {
          taskState->run(WorkerExecution{function, installer, *calls});
          std::scoped_lock activeLock(registry->mutex);
          registry->entries.erase(taskState.get());
        }));
  } catch (...) {
    std::scoped_lock activeLock(impl_->active->mutex);
    impl_->active->entries.erase(taskState.get());
    throw;
  }
  return task;
}

void LuaWorkerTasks::cancelAll() {
  std::scoped_lock lock(impl_->mutex);
  if (!impl_->active)
    return;
  std::scoped_lock activeLock(impl_->active->mutex);
  for (const auto &[key, weak] : impl_->active->entries)
    if (const auto state = weak.lock())
      state->cancel();
  impl_->mainCalls.cancelPending();
}

void LuaWorkerTasks::shutdown() {
  std::unique_ptr<JobSystem> jobs;
  std::shared_ptr<Impl::Active> active;
  {
    std::scoped_lock lock(impl_->mutex);
    impl_->stopping = true;
    impl_->main = nullptr;
    jobs = std::move(impl_->jobs);
    active = std::move(impl_->active);
    bindingLifetime_.reset();
  }
  if (active) {
    std::scoped_lock activeLock(active->mutex);
    for (const auto &[key, weak] : active->entries)
      if (const auto state = weak.lock())
        state->cancel();
  }
  impl_->mainCalls.shutdown();
  if (jobs)
    jobs->shutdown();
}

} // namespace demi::runtime
