#include "demi/runtime/scripting/LuaWorkerContext.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <cassert>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>

using namespace demi::runtime;
using namespace std::chrono_literals;

namespace {
template <class Exception, class Function> void rejects(Function function) {
  bool rejected = false;
  try {
    function();
  } catch (const Exception &) {
    rejected = true;
  }
  assert(rejected);
}

void registryLifecycle() {
  auto *state = luaL_newstate();
  auto *other = luaL_newstate();
  assert(state && other);
  lua_pushinteger(state, 42);
  auto completion = std::make_shared<AsyncCompletion>();
  assert(!luaWorkerContext(state).isWorker);
  rejects<std::runtime_error>(
      [&] { (void)waitLuaWorkerCompletion(state, completion, 0ms); });
  completion->complete();
  rejects<std::runtime_error>(
      [&] { (void)waitLuaWorkerCompletion(state, completion); });

  std::stop_source stop;
  setLuaWorkerContext(state, stop.get_token());
  assert(luaWorkerContext(state).isWorker);
  assert(luaWorkerContext(state).stopToken == stop.get_token());
  assert(!luaWorkerContext(other).isWorker);
  auto *coroutine = lua_newthread(state);
  assert(luaWorkerContext(coroutine).isWorker);
  assert(waitLuaWorkerCompletion(coroutine, completion) ==
         AsyncWaitResult::Ready);
  lua_pop(state, 1);
  rejects<std::invalid_argument>(
      [&] { (void)waitLuaWorkerCompletion(state, nullptr); });

  auto pending = std::make_shared<AsyncCompletion>();
  assert(waitLuaWorkerCompletion(state, pending, 0ms) ==
         AsyncWaitResult::Timeout);
  assert(waitLuaWorkerCompletion(state, pending, 10ms) ==
         AsyncWaitResult::Timeout);
  stop.request_stop();
  assert(waitLuaWorkerCompletion(state, pending) == AsyncWaitResult::Cancelled);
  std::stop_source replacement;
  setLuaWorkerContext(state, replacement.get_token());
  assert(luaWorkerContext(state).stopToken == replacement.get_token());
  assert(waitLuaWorkerCompletion(state, pending, 0ms) ==
         AsyncWaitResult::Timeout);
  clearLuaWorkerContext(state);
  clearLuaWorkerContext(state);
  assert(!luaWorkerContext(state).isWorker);
  assert(!luaWorkerContext(state).stopToken.stop_possible());
  setLuaWorkerContext(state, {});
  assert(luaWorkerContext(state).isWorker);
  assert(waitLuaWorkerCompletion(state, completion) == AsyncWaitResult::Ready);
  assert(lua_gettop(state) == 1 && lua_tointeger(state, 1) == 42);
  lua_close(state); // Also releases installed stop tokens through __gc.
  lua_close(other);
  rejects<std::invalid_argument>([&] { (void)luaWorkerContext(nullptr); });
  rejects<std::invalid_argument>([&] { setLuaWorkerContext(nullptr, {}); });
  rejects<std::invalid_argument>([&] { clearLuaWorkerContext(nullptr); });
  rejects<std::invalid_argument>(
      [&] { (void)waitLuaWorkerCompletion(nullptr, completion); });
}

void workerNotifications(bool cancel, bool timed) {
  std::stop_source stop;
  auto completion = std::make_shared<AsyncCompletion>();
  std::promise<void> installed;
  auto installedFuture = installed.get_future();
  auto result = std::async(std::launch::async, [&] {
    auto *state = luaL_newstate();
    assert(state);
    setLuaWorkerContext(state, stop.get_token());
    installed.set_value();
    const auto outcome = waitLuaWorkerCompletion(
        state, completion,
        timed ? std::optional{std::chrono::milliseconds{30s}} : std::nullopt);
    clearLuaWorkerContext(state);
    lua_close(state);
    return outcome;
  });
  assert(installedFuture.wait_for(2s) == std::future_status::ready);
  assert(result.wait_for(10ms) == std::future_status::timeout);
  if (cancel)
    stop.request_stop();
  else
    completion->complete();
  assert(result.wait_for(2s) == std::future_status::ready);
  assert(result.get() ==
         (cancel ? AsyncWaitResult::Cancelled : AsyncWaitResult::Ready));
  assert(completion->ready() == !cancel);
}
} // namespace

int main() {
  registryLifecycle();
  for (int iteration = 0; iteration < 25; ++iteration) {
    workerNotifications(false, false);
    workerNotifications(false, true);
    workerNotifications(true, false);
    workerNotifications(true, true);
  }
}
