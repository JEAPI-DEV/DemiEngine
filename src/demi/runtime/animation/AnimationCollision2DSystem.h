#pragma once

#include "demi/runtime/scene/model/World.h"

namespace demi::runtime {

class AnimationCollision2DSystem {
public:
  void update(World &world) const;
};

} // namespace demi::runtime
