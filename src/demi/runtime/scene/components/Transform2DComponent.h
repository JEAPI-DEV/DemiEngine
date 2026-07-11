#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct Transform2DComponent {
  std::string parent;
  Vec2 position;
  float rotation = 0.0F;
  Vec2 scale = {1.0F, 1.0F};
};

} // namespace demi::runtime
