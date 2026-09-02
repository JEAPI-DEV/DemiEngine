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

void LuaScriptHost::startMobileTests(const std::string &moduleName) {
  mobileTestsEnabled_ = true;
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
        "test", "Failed to load mobile test module '" + moduleName +
                    "': " + (message != nullptr ? message : "unknown error") +
                    "."));
    lua_pop(state, 1);
    mobileTestsEnabled_ = false;
    return;
  }
  if (!lua_istable(state, -1)) {
    lua_pop(state, 1);
    deviceLog(deviceLogMessage("test", "Mobile test module '" + moduleName +
                                           "' did not return a table."));
    mobileTestsEnabled_ = false;
    return;
  }

  lua_getfield(state, -1, "tests");
  if (!lua_istable(state, -1)) {
    lua_pop(state, 2);
    deviceLog(deviceLogMessage("test", "Mobile test module '" + moduleName +
                                           "' has no tests table."));
    mobileTestsEnabled_ = false;
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
      mobileTests_.push_back(
          {testName, static_cast<int>(luaL_ref(state, LUA_REGISTRYINDEX))});
    } else {
      lua_pop(state, 2);
      continue;
    }
    lua_pop(state, 1);
  }
  lua_pop(state, 2);

  deviceLog(deviceLogMessage(
      "test", "Running " + std::to_string(mobileTests_.size()) +
                  " mobile test(s) from '" + moduleName + "'."));
}

bool LuaScriptHost::mobileTestsActive() const {
  return mobileTestsEnabled_;
}

int LuaScriptHost::mobileTestsPassed() const {
  return mobileTestsPassed_;
}

int LuaScriptHost::mobileTestsFailed() const {
  return mobileTestsFailed_;
}

std::optional<Vec2> LuaScriptHost::mobileNodeCenterCanvas(
    const std::string &nodeId) const {
  if (world_ == nullptr)
    return std::nullopt;
  const auto found = std::ranges::find(world_->ui.nodes, nodeId, &ui::UiNode::id);
  if (found == world_->ui.nodes.end())
    return std::nullopt;
  return Vec2{found->resolved.x + found->resolved.width * 0.5F,
              found->resolved.y + found->resolved.height * 0.5F};
}

std::optional<Vec2> LuaScriptHost::mobileNodeCenterViewport(
    const std::string &nodeId) const {
  const auto center = mobileNodeCenterCanvas(nodeId);
  if (!center)
    return std::nullopt;
  return mobileCanvasToViewport(*center);
}

Vec2 LuaScriptHost::mobileCanvasToViewport(const Vec2 canvas) const {
  return Vec2{canvas.x * static_cast<float>(std::max(viewportWidth_, 1)) /
                  std::max(world_ != nullptr ? world_->ui.canvasSize.x : 960.0F,
                           1.0F),
              canvas.y * static_cast<float>(std::max(viewportHeight_, 1)) /
                  std::max(world_ != nullptr ? world_->ui.canvasSize.y : 540.0F,
                           1.0F)};
}

void LuaScriptHost::mobileEnqueueTap(const Vec2 viewportPosition) {
  syntheticTouches_.push_back({TouchPhase::Began, viewportPosition});
  syntheticTouches_.push_back({TouchPhase::Ended, viewportPosition});
  mobileWait_ = MobileWaitKind::Gesture;
  pendingGestureFrames_ = 2;
}

void LuaScriptHost::mobileEnqueueSwipe(const Vec2 from,
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
  mobileWait_ = MobileWaitKind::Gesture;
  pendingGestureFrames_ = frames;
}

void LuaScriptHost::mobileWaitFor(const double seconds) {
  mobileWait_ = MobileWaitKind::Seconds;
  mobileWaitSeconds_ = std::max(seconds, 0.0);
  mobileWaitElapsed_ = 0.0;
}

void LuaScriptHost::mobileExpectSceneStart(const std::string &sceneId,
                                           const double timeout) {
  mobileWait_ = MobileWaitKind::Scene;
  mobileWaitScene_ = sceneId;
  mobileWaitTimeout_ = std::max(timeout, 0.0);
  mobileWaitElapsed_ = 0.0;
}

void LuaScriptHost::failActiveMobileTest(const std::string &message) {
  deviceLog(deviceLogMessage("test", "FAIL " + activeMobileTestName_ + ": " +
                                         message + "."));
  ++mobileTestsFailed_;
  finishActiveMobileTest();
}

void LuaScriptHost::passActiveMobileTest() {
  deviceLog(deviceLogMessage("test", "PASS " + activeMobileTestName_ + "."));
  ++mobileTestsPassed_;
  finishActiveMobileTest();
}

void LuaScriptHost::finishActiveMobileTest() {
  if (mobileTestThread_ != 0) {
    luaL_unref(static_cast<lua_State *>(state_), LUA_REGISTRYINDEX,
               mobileTestThread_);
    mobileTestThread_ = 0;
  }
  activeMobileTestName_.clear();
  mobileWait_ = MobileWaitKind::None;
  pendingGestureFrames_ = 0;
  syntheticTouches_.clear();
  ++mobileTestIndex_;
}

void LuaScriptHost::drainSyntheticTouches(InputState &input) {
  if (syntheticTouches_.empty())
    return;
  const MobileSyntheticFrame frame = syntheticTouches_.front();
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

void LuaScriptHost::startNextMobileTest() {
  auto *state = static_cast<lua_State *>(state_);
  if (mobileTestIndex_ >= mobileTests_.size()) {
    deviceLog(deviceLogMessage(
        "test", "SUMMARY passed=" + std::to_string(mobileTestsPassed_) +
                    " failed=" + std::to_string(mobileTestsFailed_) + "."));
    mobileTestsEnabled_ = false;
    requestQuit();
    return;
  }
  const MobileTestDefinition &definition = mobileTests_[mobileTestIndex_];
  activeMobileTestName_ = definition.name;
  lua_State *thread = lua_newthread(state);
  mobileTestThread_ =
      static_cast<int>(luaL_ref(state, LUA_REGISTRYINDEX));
  lua_rawgeti(thread, LUA_REGISTRYINDEX, definition.functionRef);
  mobileWait_ = MobileWaitKind::None;
}

void LuaScriptHost::updateMobileTests(const double deltaTime) {
  if (!mobileTestsEnabled_)
    return;
  auto *state = static_cast<lua_State *>(state_);
  if (state == nullptr)
    return;

  if (mobileTestThread_ == 0) {
    startNextMobileTest();
    if (mobileTestThread_ == 0)
      return; // The summary ran and requested quit.
  } else {
    switch (mobileWait_) {
    case MobileWaitKind::Frames:
      if (--mobileWaitFrames_ > 0)
        return;
      break;
    case MobileWaitKind::Seconds:
      mobileWaitElapsed_ += deltaTime;
      if (mobileWaitElapsed_ < mobileWaitSeconds_)
        return;
      break;
    case MobileWaitKind::Scene: {
      const std::string &active =
          world_ != nullptr ? world_->activeSceneId : std::string{};
      if (active == mobileWaitScene_) {
        break;
      }
      mobileWaitElapsed_ += deltaTime;
      if (mobileWaitElapsed_ >= mobileWaitTimeout_) {
        failActiveMobileTest("Timed out waiting for scene " +
                             mobileWaitScene_ + " (active: " + active + ")");
        return;
      }
      return;
    }
    case MobileWaitKind::Gesture:
      if (pendingGestureFrames_ > 0)
        return;
      break;
    case MobileWaitKind::None:
      break;
    }
    mobileWait_ = MobileWaitKind::None;
  }

  lua_rawgeti(state, LUA_REGISTRYINDEX, mobileTestThread_);
  lua_State *thread = lua_tothread(state, -1);
  if (thread == nullptr) {
    lua_pop(state, 1);
    failActiveMobileTest("Lost the mobile test coroutine.");
    return;
  }
  int results = 0;
  const int status = lua_resume(thread, state, 0, &results);
  lua_pop(state, 1);
  if (status == LUA_YIELD) {
    if (mobileWait_ == MobileWaitKind::None) {
      failActiveMobileTest("Test yielded without a wait request.");
    }
    return;
  }
  if (status != LUA_OK) {
    const char *message = lua_tostring(thread, -1);
    failActiveMobileTest(message != nullptr ? message : "unknown error");
    lua_pop(thread, 1);
    return;
  }
  passActiveMobileTest();
}

} // namespace demi::runtime
