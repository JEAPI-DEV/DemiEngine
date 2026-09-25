#include "demi/runtime/scripting/LuaE2ETestRunner.h"

#include "demi/runtime/diagnostics/DeviceLog.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <cmath>
#include <utility>

namespace demi::runtime {

namespace {
constexpr std::int64_t SyntheticFingerId = 0x54455354LL; // 'TEST'
} // namespace

LuaE2ETestRunner::~LuaE2ETestRunner() { shutdown(); }

void LuaE2ETestRunner::initialize(lua_State *state,
                                  RuntimeCallbacks callbacks) {
  shutdown();
  state_ = state;
  callbacks_ = std::move(callbacks);
}

void LuaE2ETestRunner::releaseReferences() {
  if (state_ != nullptr) {
    if (coroutineRef_ != 0) {
      luaL_unref(state_, LUA_REGISTRYINDEX, coroutineRef_);
    }
    for (const TestDefinition &test : tests_) {
      luaL_unref(state_, LUA_REGISTRYINDEX, test.functionRef);
    }
  }
  coroutineRef_ = 0;
  tests_.clear();
  activeTestName_.clear();
  wait_ = WaitKind::None;
  pendingGestureFrames_ = 0;
  syntheticTouches_.clear();
}

void LuaE2ETestRunner::shutdown() {
  releaseReferences();
  active_ = false;
  state_ = nullptr;
  callbacks_ = {};
}

void LuaE2ETestRunner::failStartup(const std::string &message) {
  deviceLog(deviceLogMessage("test", message));
  ++failed_;
  complete();
}

void LuaE2ETestRunner::complete() {
  deviceLog(
      deviceLogMessage("test", "SUMMARY passed=" + std::to_string(passed_) +
                                   " failed=" + std::to_string(failed_) + "."));
  releaseReferences();
  active_ = false;
  if (callbacks_.requestQuit) {
    callbacks_.requestQuit();
  }
}

void LuaE2ETestRunner::start(const std::string &moduleName) {
  releaseReferences();
  testIndex_ = 0;
  passed_ = 0;
  failed_ = 0;
  active_ = true;
  lua_State *state = state_;
  if (state == nullptr) {
    failStartup("Lua VM is unavailable.");
    return;
  }

  lua_getglobal(state, "require");
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 1);
    failStartup("Lua require is unavailable.");
    return;
  }
  lua_pushlstring(state, moduleName.data(), moduleName.size());
  if (lua_pcall(state, 1, 1, 0) != LUA_OK) {
    const char *message = lua_tostring(state, -1);
    const std::string error =
        "Failed to load e2e test module '" + moduleName +
        "': " + (message != nullptr ? message : "unknown error") + ".";
    lua_pop(state, 1);
    failStartup(error);
    return;
  }
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    failStartup("E2e test module '" + moduleName + "' did not return a table.");
    return;
  }

  lua_getfield(state, -1, "tests");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 2);
    failStartup("E2e test module '" + moduleName + "' has no tests table.");
    return;
  }

  const std::size_t testCount = lua_rawlen(state, -1);
  for (std::size_t index = 1; index <= testCount; ++index) {
    lua_rawgeti(state, -1, static_cast<int>(index));
    if (!lua_istable(state, -1)) {
      lua_pop(state, 1);
      continue;
    }
    lua_getfield(state, -1, "name");
    const char *name = lua_tostring(state, -1);
    const std::string testName = name != nullptr ? name : "unnamed";
    lua_pop(state, 1);
    lua_getfield(state, -1, "func");
    if (lua_isfunction(state, -1)) {
      tests_.push_back(
          {testName, static_cast<int>(luaL_ref(state, LUA_REGISTRYINDEX))});
    } else {
      lua_pop(state, 2);
      continue;
    }
    lua_pop(state, 1);
  }
  lua_pop(state, 2);

  deviceLog(
      deviceLogMessage("test", "Running " + std::to_string(tests_.size()) +
                                   " e2e test(s) from '" + moduleName + "'."));
}

bool LuaE2ETestRunner::active() const { return active_; }

int LuaE2ETestRunner::passed() const { return passed_; }

int LuaE2ETestRunner::failed() const { return failed_; }

std::optional<Vec2>
LuaE2ETestRunner::nodeCenterCanvas(const std::string &nodeId) const {
  return callbacks_.nodeCenterCanvas ? callbacks_.nodeCenterCanvas(nodeId)
                                     : std::nullopt;
}

std::optional<Vec2>
LuaE2ETestRunner::nodeCenterViewport(const std::string &nodeId) const {
  const auto center = nodeCenterCanvas(nodeId);
  if (!center)
    return std::nullopt;
  return canvasToViewport(*center);
}

Vec2 LuaE2ETestRunner::canvasToViewport(const Vec2 canvas) const {
  return callbacks_.canvasToViewport ? callbacks_.canvasToViewport(canvas)
                                     : canvas;
}

void LuaE2ETestRunner::enqueueTap(const Vec2 viewportPosition) {
  syntheticTouches_.push_back({TouchPhase::Began, viewportPosition});
  syntheticTouches_.push_back({TouchPhase::Ended, viewportPosition});
  wait_ = WaitKind::Gesture;
  pendingGestureFrames_ = 2;
}

void LuaE2ETestRunner::enqueueSwipe(const Vec2 from, const Vec2 to,
                                    const double seconds) {
  const int frames = std::max(2, static_cast<int>(std::ceil(seconds * 60.0)));
  const Vec2 delta{(to.x - from.x) / static_cast<float>(frames - 1),
                   (to.y - from.y) / static_cast<float>(frames - 1)};
  Vec2 position = from;
  syntheticTouches_.push_back({TouchPhase::Began, position});
  for (int frame = 1; frame < frames - 1; ++frame) {
    position = Vec2{position.x + delta.x, position.y + delta.y};
    syntheticTouches_.push_back({TouchPhase::Moved, position});
  }
  syntheticTouches_.push_back({TouchPhase::Ended, to});
  wait_ = WaitKind::Gesture;
  pendingGestureFrames_ = frames;
}

void LuaE2ETestRunner::waitFor(const double seconds) {
  wait_ = WaitKind::Seconds;
  waitSeconds_ = std::max(seconds, 0.0);
  waitElapsed_ = 0.0;
}

void LuaE2ETestRunner::expectScene(const std::string &sceneId,
                                   const double timeout) {
  wait_ = WaitKind::Scene;
  waitScene_ = sceneId;
  waitTimeout_ = std::max(timeout, 0.0);
  waitElapsed_ = 0.0;
}

void LuaE2ETestRunner::failTest(const std::string &message) {
  deviceLog(deviceLogMessage("test",
                             "FAIL " + activeTestName_ + ": " + message + "."));
  ++failed_;
  finishTest();
}

void LuaE2ETestRunner::passTest() {
  deviceLog(deviceLogMessage("test", "PASS " + activeTestName_ + "."));
  ++passed_;
  finishTest();
}

void LuaE2ETestRunner::finishTest() {
  if (coroutineRef_ != 0) {
    luaL_unref(state_, LUA_REGISTRYINDEX, coroutineRef_);
    coroutineRef_ = 0;
  }
  activeTestName_.clear();
  wait_ = WaitKind::None;
  pendingGestureFrames_ = 0;
  syntheticTouches_.clear();
  ++testIndex_;
}

void LuaE2ETestRunner::drainSyntheticTouches(InputState &input) {
  if (syntheticTouches_.empty())
    return;
  const SyntheticFrame frame = syntheticTouches_.front();
  syntheticTouches_.pop_front();
  const auto existing =
      std::ranges::find(input.touches, SyntheticFingerId, &TouchPoint::id);
  const TouchPoint value{.id = SyntheticFingerId,
                         .phase = frame.phase,
                         .position = frame.position,
                         .delta = {},
                         .pressure = 1.0F};
  if (existing == input.touches.end())
    input.touches.push_back(value);
  else
    *existing = value;
  if (pendingGestureFrames_ > 0)
    --pendingGestureFrames_;
}

void LuaE2ETestRunner::startNextTest() {
  lua_State *state = state_;
  if (testIndex_ >= tests_.size()) {
    complete();
    return;
  }
  const TestDefinition &definition = tests_[testIndex_];
  activeTestName_ = definition.name;
  lua_State *thread = lua_newthread(state);
  coroutineRef_ = static_cast<int>(luaL_ref(state, LUA_REGISTRYINDEX));
  lua_rawgeti(thread, LUA_REGISTRYINDEX, definition.functionRef);
  wait_ = WaitKind::None;
}

void LuaE2ETestRunner::update(const double deltaTime) {
  if (!active_)
    return;
  lua_State *state = state_;
  if (state == nullptr)
    return;

  if (coroutineRef_ == 0) {
    startNextTest();
    if (coroutineRef_ == 0)
      return; // The summary ran and requested quit.
  } else {
    switch (wait_) {
    case WaitKind::Seconds:
      waitElapsed_ += deltaTime;
      if (waitElapsed_ < waitSeconds_)
        return;
      break;
    case WaitKind::Scene: {
      const std::string active =
          callbacks_.activeScene ? callbacks_.activeScene() : std::string{};
      if (active == waitScene_) {
        break;
      }
      waitElapsed_ += deltaTime;
      if (waitElapsed_ >= waitTimeout_) {
        failTest("Timed out waiting for scene " + waitScene_ +
                 " (active: " + active + ")");
        return;
      }
      return;
    }
    case WaitKind::Gesture:
      if (pendingGestureFrames_ > 0)
        return;
      break;
    case WaitKind::None:
      break;
    }
    wait_ = WaitKind::None;
  }

  lua_rawgeti(state, LUA_REGISTRYINDEX, coroutineRef_);
  lua_State *thread = lua_tothread(state, -1);
  if (thread == nullptr) {
    lua_pop(state, 1);
    failTest("Lost the e2e test coroutine.");
    return;
  }
  int results = 0;
  const int status = lua_resume(thread, state, 0, &results);
  lua_pop(state, 1);
  if (status == LUA_YIELD) {
    if (wait_ == WaitKind::None) {
      failTest("Test yielded without a wait request.");
    }
    return;
  }
  if (status != LUA_OK) {
    const char *message = lua_tostring(thread, -1);
    const std::string error = message != nullptr ? message : "unknown error";
    lua_pop(thread, 1);
    failTest(error);
    return;
  }
  passTest();
}

} // namespace demi::runtime
