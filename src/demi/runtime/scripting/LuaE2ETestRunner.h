#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct lua_State;

namespace demi::runtime {

// Owns Lua registry references; shutdown must run before the attached VM
// closes.
class LuaE2ETestRunner {
public:
  struct RuntimeCallbacks {
    std::function<std::optional<Vec2>(const std::string &)> nodeCenterCanvas;
    std::function<Vec2(Vec2)> canvasToViewport;
    std::function<std::string()> activeScene;
    std::function<void()> requestQuit;
  };

  LuaE2ETestRunner() = default;
  ~LuaE2ETestRunner();
  LuaE2ETestRunner(const LuaE2ETestRunner &) = delete;
  LuaE2ETestRunner &operator=(const LuaE2ETestRunner &) = delete;

  void initialize(lua_State *state, RuntimeCallbacks callbacks);
  void shutdown();
  void start(const std::string &moduleName);
  void update(double deltaTime);
  void drainSyntheticTouches(InputState &input);
  [[nodiscard]] bool active() const;
  [[nodiscard]] int passed() const;
  [[nodiscard]] int failed() const;
  [[nodiscard]] std::optional<Vec2>
  nodeCenterCanvas(const std::string &nodeId) const;
  [[nodiscard]] std::optional<Vec2>
  nodeCenterViewport(const std::string &nodeId) const;
  [[nodiscard]] Vec2 canvasToViewport(Vec2 canvas) const;
  void enqueueTap(Vec2 viewportPosition);
  void enqueueSwipe(Vec2 from, Vec2 to, double seconds);
  void waitFor(double seconds);
  void expectScene(const std::string &sceneId, double timeout);

private:
  struct SyntheticFrame {
    TouchPhase phase = TouchPhase::Began;
    Vec2 position;
  };
  struct TestDefinition {
    std::string name;
    int functionRef = 0;
  };
  enum class WaitKind { None, Seconds, Scene, Gesture };

  void releaseReferences();
  void failStartup(const std::string &message);
  void complete();
  void failTest(const std::string &message);
  void passTest();
  void finishTest();
  void startNextTest();

  lua_State *state_ = nullptr;
  RuntimeCallbacks callbacks_;
  std::vector<TestDefinition> tests_;
  std::size_t testIndex_ = 0;
  int coroutineRef_ = 0;
  std::string activeTestName_;
  WaitKind wait_ = WaitKind::None;
  double waitSeconds_ = 0.0;
  double waitElapsed_ = 0.0;
  std::string waitScene_;
  double waitTimeout_ = 0.0;
  int pendingGestureFrames_ = 0;
  std::deque<SyntheticFrame> syntheticTouches_;
  int passed_ = 0;
  int failed_ = 0;
  bool active_ = false;
};

} // namespace demi::runtime
