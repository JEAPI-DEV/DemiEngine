#pragma once

#include "demi/runtime/scene/model/World.h"

namespace demi::runtime {

class AudioSystem;

class AudioSceneSystem {
public:
  void update(World &world, AudioSystem &audio) const;
};

} // namespace demi::runtime
