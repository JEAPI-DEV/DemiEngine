#include "demi/runtime/scripting/LuaScriptHost.h"

#include <algorithm>

namespace demi::runtime {

void LuaScriptHost::initializeE2ETestRunner() {
  e2eTestRunner_.initialize(
      static_cast<lua_State *>(state_),
      {
          .nodeCenterCanvas =
              [this](const std::string &nodeId) -> std::optional<Vec2> {
            if (world_ == nullptr) {
              return std::nullopt;
            }
            const auto found =
                std::ranges::find(world_->ui.nodes, nodeId, &ui::UiNode::id);
            if (found == world_->ui.nodes.end()) {
              return std::nullopt;
            }
            return Vec2{found->resolved.x + found->resolved.width * 0.5F,
                        found->resolved.y + found->resolved.height * 0.5F};
          },
          .canvasToViewport =
              [this](Vec2 canvas) {
                const Vec2 canvasSize = world_ != nullptr
                                            ? world_->ui.canvasSize
                                            : Vec2{960.0F, 540.0F};
                return Vec2{
                    canvas.x * static_cast<float>(std::max(viewportWidth_, 1)) /
                        std::max(canvasSize.x, 1.0F),
                    canvas.y *
                        static_cast<float>(std::max(viewportHeight_, 1)) /
                        std::max(canvasSize.y, 1.0F)};
              },
          .activeScene = [this] { return activeSceneId(); },
          .requestQuit = [this] { requestQuit(); },
      });
}

LuaE2ETestRunner &LuaScriptHost::e2eTestRunner() { return e2eTestRunner_; }

void LuaScriptHost::startE2ETests(const std::string &moduleName) {
  e2eTestRunner_.start(moduleName);
}

void LuaScriptHost::updateE2ETests(double deltaTime) {
  e2eTestRunner_.update(deltaTime);
}

void LuaScriptHost::drainSyntheticTouches(InputState &input) {
  e2eTestRunner_.drainSyntheticTouches(input);
}

bool LuaScriptHost::e2eTestsActive() const { return e2eTestRunner_.active(); }

int LuaScriptHost::e2eTestsPassed() const { return e2eTestRunner_.passed(); }

int LuaScriptHost::e2eTestsFailed() const { return e2eTestRunner_.failed(); }

} // namespace demi::runtime
