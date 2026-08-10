#pragma once

#include "demi/runtime/scene/model/World.h"

namespace demi::runtime {

class Camera2DSystem {
public:
  void update(World &world, float deltaTime,
              float physicsInterpolationAlpha = 1.0F) const;
};

} // namespace demi::runtime
