#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct Transform3DComponent {
  std::string parent;
  Vec3 position;
  Vec3 rotation;
  Vec3 scale = {1.0F, 1.0F, 1.0F};
};

} // namespace demi::runtime
