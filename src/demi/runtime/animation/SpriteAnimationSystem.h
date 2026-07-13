#pragma once

#include "demi/runtime/scene/model/World.h"

namespace demi::runtime {

class SpriteAnimationSystem {
public:
  void update(World &world, float deltaTime) const;
};

} // namespace demi::runtime
