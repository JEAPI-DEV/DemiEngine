#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/scripting/LuaScriptHostInternal.h"

#include <algorithm>

namespace demi::runtime {

bool LuaScriptHost::isMouseDown(const std::string &button) const {
  return input_ != nullptr &&
         input_->mouseButtonsDown.contains(normalizedKey(button));
}

Vec2 LuaScriptHost::mousePosition() const {
  return input_ != nullptr ? input_->mousePosition : Vec2{};
}
Vec2 LuaScriptHost::mouseDelta() const {
  return input_ != nullptr ? input_->mouseDelta : Vec2{};
}

Vec2 LuaScriptHost::mouseWorldPosition() const {
  if (input_ == nullptr || world_ == nullptr)
    return {};
  const Camera2DComponent *camera = activeCamera(*world_);
  const Vec2 cameraPosition = activeCameraPosition(*world_);
  const float orthographicSize =
      camera != nullptr ? camera->orthographicSize : 10.0F;
  const float pixelsPerUnit = static_cast<float>(viewportHeight_) /
                              std::max(orthographicSize * 2.0F, 1.0F);
  return {
      .x = cameraPosition.x + (input_->mousePosition.x -
                               static_cast<float>(viewportWidth_) * 0.5F) /
                                  pixelsPerUnit,
      .y = cameraPosition.y + (static_cast<float>(viewportHeight_) * 0.5F -
                               input_->mousePosition.y) /
                                  pixelsPerUnit,
  };
}

Vec2 LuaScriptHost::viewportSize() const {
  return {static_cast<float>(viewportWidth_),
          static_cast<float>(viewportHeight_)};
}

void LuaScriptHost::addDebugLine(float x1, float y1, float x2, float y2,
                                 float r, float g, float b, float a) {
  if (world_ != nullptr)
    world_->debugLines.push_back(
        {.start = {x1, y1}, .end = {x2, y2}, .color = {r, g, b, a}});
}

void LuaScriptHost::clearDebugLines() {
  if (world_ != nullptr)
    world_->debugLines.clear();
}

} // namespace demi::runtime
