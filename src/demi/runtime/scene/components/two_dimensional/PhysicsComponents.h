#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct Rigidbody2DComponent {
  std::string bodyType = "dynamic";
  Vec2 velocity;
  float gravityScale = 1.0F;
  float bounciness = 0.0F;
  bool lockRotation = true;
};

struct BoxCollider2DComponent {
  Vec2 size = {1.0F, 1.0F};
  Vec2 offset;
  bool isTrigger = false;
  std::string layer;
};

} // namespace demi::runtime
