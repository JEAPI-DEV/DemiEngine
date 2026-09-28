#include "demi/runtime/scripting/LuaMainThreadCalls.h"

#include "demi/runtime/concurrency/AsyncCompletion.h"
#include "demi/runtime/scripting/LuaTaskFunction.h"
#include "demi/runtime/scripting/LuaWorkerContext.h"

extern "C" {
#include <lua.h>
}

#include <algorithm>
#include <cstdint>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace demi::runtime {
namespace {
struct Request {
  LuaTaskFunction function;
  std::uint64_t generation = 0;
  std::stop_token stopToken;
  bool running = false;
  bool cancelled = false;
  bool failed = false;
  LuaTransferGraph results;
  std::string error;
  std::shared_ptr<AsyncCompletion> completion =
      std::make_shared<AsyncCompletion>();
};

int failure(lua_State *state, const std::string &error) {
  lua_pushnil(state);
  lua_pushlstring(state, error.data(), error.size());
  return 2;
}
} // namespace

struct LuaMainThreadCalls::Impl {
  std::mutex mutex;
  lua_State *main = nullptr;
  std::thread::id thread;
  std::uint64_t generation = 0;
  std::deque<std::shared_ptr<Request>> queue;
  std::vector<std::weak_ptr<Request>> pending;
};

LuaMainThreadCalls::LuaMainThreadCalls() : impl_(std::make_unique<Impl>()) {}
LuaMainThreadCalls::~LuaMainThreadCalls() { shutdown(); }

void LuaMainThreadCalls::attach(lua_State *state) {
  if (!state)
    throw std::invalid_argument("Task.main requires a main Lua state");
  shutdown();
  std::scoped_lock lock(impl_->mutex);
  impl_->main = state;
  impl_->thread = std::this_thread::get_id();
}

int LuaMainThreadCalls::call(lua_State *worker) {
  const auto context = luaWorkerContext(worker);
  if (!context.isWorker)
    return failure(worker, "Task.main is only available in workers");
  try {
    auto request = std::make_shared<Request>();
    request->stopToken = context.stopToken;
    {
      std::scoped_lock lock(impl_->mutex);
      request->generation = impl_->generation;
    }
    if (context.stopToken.stop_requested())
      return failure(worker, "cancelled");
    request->function = captureLuaTaskFunction(
        worker, 1, 2, lua_gettop(worker) - 1, "Task.main");
    std::string submissionError;
    {
      std::scoped_lock lock(impl_->mutex);
      if (!impl_->main)
        submissionError = "Task.main is shut down";
      else if (request->generation != impl_->generation ||
               context.stopToken.stop_requested())
        submissionError = "cancelled";
      else {
        std::erase_if(impl_->pending,
                      [](const auto &item) { return item.expired(); });
        impl_->pending.push_back(request);
        impl_->queue.push_back(request);
      }
    }
    if (!submissionError.empty())
      return failure(worker, submissionError);
    const auto waitResult =
        waitLuaWorkerCompletion(worker, request->completion);
    if (waitResult != AsyncWaitResult::Ready ||
        context.stopToken.stop_requested()) {
      {
        std::scoped_lock lock(impl_->mutex);
        request->cancelled = true;
      }
      return failure(worker, "cancelled");
    }
    // Completion publishes immutable result data before waking the worker.
    if (request->failed)
      return failure(worker, request->error);
    pushLuaValues(worker, request->results);
    return static_cast<int>(request->results.roots.size());
  } catch (const std::exception &error) {
    return failure(worker, error.what());
  }
}

void LuaMainThreadCalls::dispatch() {
  std::deque<std::shared_ptr<Request>> snapshot;
  lua_State *state;
  {
    std::scoped_lock lock(impl_->mutex);
    state = impl_->main;
    if (!state)
      return;
    if (impl_->thread != std::this_thread::get_id())
      throw std::logic_error("Task.main dispatch must run on the main thread");
    snapshot.swap(impl_->queue);
    std::erase_if(impl_->pending,
                  [](const auto &request) { return request.expired(); });
  }
  for (const auto &request : snapshot) {
    {
      std::scoped_lock lock(impl_->mutex);
      if (request->cancelled || request->stopToken.stop_requested() ||
          request->generation != impl_->generation || impl_->main != state)
        continue;
      request->running = true;
    }
    const int top = lua_gettop(state);
    try {
      const int count = invokeLuaTaskFunction(state, request->function);
      request->results = captureLuaValues(state, top + 1, count);
    } catch (const std::exception &error) {
      request->failed = true;
      request->error = error.what();
    }
    lua_settop(state, top);
    request->completion->complete();
  }
}

void LuaMainThreadCalls::cancelPending() {
  std::vector<std::shared_ptr<Request>> cancelled;
  {
    std::scoped_lock lock(impl_->mutex);
    ++impl_->generation;
    for (const auto &weak : impl_->pending) {
      if (auto request = weak.lock();
          request && !request->running && !request->cancelled) {
        request->cancelled = true;
        request->failed = true;
        request->error = "cancelled";
        cancelled.push_back(std::move(request));
      }
    }
    impl_->pending.clear();
    impl_->queue.clear();
  }
  for (const auto &request : cancelled)
    request->completion->complete();
}

void LuaMainThreadCalls::shutdown() {
  {
    std::scoped_lock lock(impl_->mutex);
    impl_->main = nullptr;
  }
  cancelPending();
}

} // namespace demi::runtime
