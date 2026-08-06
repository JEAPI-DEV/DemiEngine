#pragma once

#include "demi/runtime/scene/model/World.h"

namespace demi::runtime {

class AnimationStateMachineSystem {
public:
  void update(World &world, float deltaTime,
              float unscaledDeltaTime = -1.0F) const;
};

} // namespace demi::runtime
