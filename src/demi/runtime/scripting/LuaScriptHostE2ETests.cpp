#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/diagnostics/DeviceLog.h"

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <cmath>

namespace demi::runtime {

namespace {
constexpr std::int64_t SyntheticFingerId = 0x54455354LL; // 'TEST'
} // namespace

void LuaScriptHost::startE2ETests(const std::string &moduleName) {
  e2eTestsEnabled_ = true;
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr) {
    return;
  }

  lua_getglobal(state, "require");
  if (!lua_isfunction(state, -1)) {
    lua_pop(state, 1);
    deviceLog(deviceLogMessage("test", "Lua require is unavailable."));
    return;
  }
  lua_pushlstring(state, moduleName.data(), moduleName.size());
  if (lua_pcall(state, 1, 1, 0) != LUA_OK) {
    const char *message = lua_tostring(state, -1);
    deviceLog(deviceLogMessage(
        "test", "Failed to load e2e test module '" + moduleName +
                    "': " + (message != nullptr ? message : "unknown error") +
                    "."));
    lua_pop(state, 1);
    e2eTestsEnabled_ = false;
    return;
  }
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    deviceLog(deviceLogMessage("test", "E2e test module '" + moduleName +
                                           "' did not return a table."));
    e2eTestsEnabled_ = false;
    return;
  }

  lua_getfield(state, -1, "tests");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 2);
    deviceLog(deviceLogMessage("test", "E2e test module '" + moduleName +
                                           "' has no tests table."));
    e2eTestsEnabled_ = false;
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
    const std::string testName =
        name != nullptr ? name : "unnamed";
    lua_pop(state, 1);
    lua_getfield(state, -1, "func");
    if (lua_isfunction(state, -1)) {
      e2eTests_.push_back(
          {testName, static_cast<int>(luaL_ref(state, LUA_REGISTRYINDEX))});
    } else {
      lua_pop(state, 2);
      continue;
    }
    lua_pop(state, 1);
  }
  lua_pop(state, 2);

  deviceLog(deviceLogMessage(
      "test", "Running " + std::to_string(e2eTests_.size()) +
                  " e2e test(s) from '" + moduleName + "'."));
}

bool LuaScriptHost::e2eTestsActive() const {
  return e2eTestsEnabled_;
}

int LuaScriptHost::e2eTestsPassed() const {
  return e2eTestsPassed_;
}

int LuaScriptHost::e2eTestsFailed() const {
  return e2eTestsFailed_;
}

std::optional<Vec2> LuaScriptHost::e2eNodeCenterCanvas(
    const std::string &nodeId) const {
  if (world_ == nullptr)
    return std::nullopt;
  const auto found = std::ranges::find(world_->ui.nodes, nodeId, &ui::UiNode::id);
  if (found == world_->ui.nodes.end())
    return std::nullopt;
  return Vec2{found->resolved.x + found->resolved.width * 0.5F,
              found->resolved.y + found->resolved.height * 0.5F};
}

std::optional<Vec2> LuaScriptHost::e2eNodeCenterViewport(
    const std::string &nodeId) const {
  const auto center = e2eNodeCenterCanvas(nodeId);
  if (!center)
    return std::nullopt;
  return e2eCanvasToViewport(*center);
}

Vec2 LuaScriptHost::e2eCanvasToViewport(const Vec2 canvas) const {
  return Vec2{canvas.x * static_cast<float>(std::max(viewportWidth_, 1)) /
                  std::max(world_ != nullptr ? world_->ui.canvasSize.x : 960.0F,
                           1.0F),
              canvas.y * static_cast<float>(std::max(viewportHeight_, 1)) /
                  std::max(world_ != nullptr ? world_->ui.canvasSize.y : 540.0F,
                           1.0F)};
}

void LuaScriptHost::e2eEnqueueTap(const Vec2 viewportPosition) {
  syntheticTouches_.push_back({TouchPhase::Began, viewportPosition});
  syntheticTouches_.push_back({TouchPhase::Ended, viewportPosition});
  e2eWait_ = E2EWaitKind::Gesture;
  pendingGestureFrames_ = 2;
}

void LuaScriptHost::e2eEnqueueSwipe(const Vec2 from,
                                       const Vec2 to, const double seconds) {
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
  e2eWait_ = E2EWaitKind::Gesture;
  pendingGestureFrames_ = frames;
}

void LuaScriptHost::e2eWaitFor(const double seconds) {
  e2eWait_ = E2EWaitKind::Seconds;
  e2eWaitSeconds_ = std::max(seconds, 0.0);
  e2eWaitElapsed_ = 0.0;
}

void LuaScriptHost::e2eExpectSceneStart(const std::string &sceneId,
                                           const double timeout) {
  e2eWait_ = E2EWaitKind::Scene;
  e2eWaitScene_ = sceneId;
  e2eWaitTimeout_ = std::max(timeout, 0.0);
  e2eWaitElapsed_ = 0.0;
}

void LuaScriptHost::failActiveE2ETest(const std::string &message) {
  deviceLog(deviceLogMessage("test", "FAIL " + activeE2ETestName_ + ": " +
                                         message + "."));
  ++e2eTestsFailed_;
  finishActiveE2ETest();
}

void LuaScriptHost::passActiveE2ETest() {
  deviceLog(deviceLogMessage("test", "PASS " + activeE2ETestName_ + "."));
  ++e2eTestsPassed_;
  finishActiveE2ETest();
}

void LuaScriptHost::finishActiveE2ETest() {
  if (e2eTestThread_ != 0) {
    luaL_unref(static_cast<lua_State *>(state_), LUA_REGISTRYINDEX,
               e2eTestThread_);
    e2eTestThread_ = 0;
  }
  activeE2ETestName_.clear();
  e2eWait_ = E2EWaitKind::None;
  pendingGestureFrames_ = 0;
  syntheticTouches_.clear();
  ++e2eTestIndex_;
}

void LuaScriptHost::drainSyntheticTouches(InputState &input) {
  if (syntheticTouches_.empty())
    return;
  const E2ESyntheticFrame frame = syntheticTouches_.front();
  syntheticTouches_.pop_front();
  const auto existing = std::ranges::find(input.touches, syntheticFingerId_,
                                          &TouchPoint::id);
  const TouchPoint value{.id = syntheticFingerId_,
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

void LuaScriptHost::startNextE2ETest() {
  auto *state = static_cast<lua_State *>(state_);
  if (e2eTestIndex_ >= e2eTests_.size()) {
    deviceLog(deviceLogMessage(
        "test", "SUMMARY passed=" + std::to_string(e2eTestsPassed_) +
                    " failed=" + std::to_string(e2eTestsFailed_) + "."));
    e2eTestsEnabled_ = false;
    requestQuit();
    return;
  }
  const E2ETestDefinition &definition = e2eTests_[e2eTestIndex_];
  activeE2ETestName_ = definition.name;
  lua_State *thread = lua_newthread(state);
  e2eTestThread_ =
      static_cast<int>(luaL_ref(state, LUA_REGISTRYINDEX));
  lua_rawgeti(thread, LUA_REGISTRYINDEX, definition.functionRef);
  e2eWait_ = E2EWaitKind::None;
}

void LuaScriptHost::updateE2ETests(const double deltaTime) {
  if (!e2eTestsEnabled_)
    return;
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr)
    return;

  if (e2eTestThread_ == 0) {
    startNextE2ETest();
    if (e2eTestThread_ == 0)
      return; // The summary ran and requested quit.
  } else {
    switch (e2eWait_) {
    case E2EWaitKind::Frames:
      if (--e2eWaitFrames_ > 0)
        return;
      break;
    case E2EWaitKind::Seconds:
      e2eWaitElapsed_ += deltaTime;
      if (e2eWaitElapsed_ < e2eWaitSeconds_)
        return;
      break;
    case E2EWaitKind::Scene: {
      const std::string &active =
          world_ != nullptr ? world_->activeSceneId : std::string{};
      if (active == e2eWaitScene_) {
        break;
      }
      e2eWaitElapsed_ += deltaTime;
      if (e2eWaitElapsed_ >= e2eWaitTimeout_) {
        failActiveE2ETest("Timed out waiting for scene " +
                             e2eWaitScene_ + " (active: " + active + ")");
        return;
      }
      return;
    }
    case E2EWaitKind::Gesture:
      if (pendingGestureFrames_ > 0)
        return;
      break;
    case E2EWaitKind::None:
      break;
    }
    e2eWait_ = E2EWaitKind::None;
  }

  lua_rawgeti(state, LUA_REGISTRYINDEX, e2eTestThread_);
  lua_State *thread = lua_tothread(state, -1);
  if (thread == nullptr) {
    lua_pop(state, 1);
    failActiveE2ETest("Lost the e2e test coroutine.");
    return;
  }
  int results = 0;
  const int status = lua_resume(thread, state, 0, &results);
  lua_pop(state, 1);
  if (status == LUA_YIELD) {
    if (e2eWait_ == E2EWaitKind::None) {
      failActiveE2ETest("Test yielded without a wait request.");
    }
    return;
  }
  if (status != LUA_OK) {
    const char *message = lua_tostring(thread, -1);
    failActiveE2ETest(message != nullptr ? message : "unknown error");
    lua_pop(thread, 1);
    return;
  }
  passActiveE2ETest();
}

} // namespace demi::runtime
