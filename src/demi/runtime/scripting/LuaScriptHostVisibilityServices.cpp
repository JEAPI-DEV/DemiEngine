#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/profiling/RuntimeProfiler.h"

#include "demi/runtime/visibility/VisibilityQueries2D.h"

#include <cmath>

namespace demi::runtime {

bool LuaScriptHost::configureFog2D(int width, int height, float cellSize,
                                   Vec2 origin, Color color,
                                   float memoryOpacity) {
  return world_ && world_->fog2D.configure(width, height, cellSize, origin,
                                           color, memoryOpacity);
}

void LuaScriptHost::revealFog2D(int x, int y) {
  if (world_) {
    world_->fog2D.reveal(x, y);
  }
}

void LuaScriptHost::clearFog2D() {
  if (world_) {
    world_->fog2D.clear();
  }
}

bool LuaScriptHost::updateFog2D(Vec2 focus, float radius, float feather,
                                const std::string &occluderLayer) {
  if (!world_ || !std::isfinite(focus.x) || !std::isfinite(focus.y) ||
      !std::isfinite(radius) || radius <= 0.0F || radius > 256.0F ||
      !std::isfinite(feather) || feather <= 0.0F) {
    return false;
  }
  ProfileScope fogScope("Visibility.fog2d");
  const auto boundary =
      castVisibilityBoundary2D(*world_, focus, radius, occluderLayer);
  ProfileScope maskScope("Visibility.publish2d");
  return world_->fog2D.update(focus, radius, feather, boundary.distances,
                              boundary.angles);
}

} // namespace demi::runtime
