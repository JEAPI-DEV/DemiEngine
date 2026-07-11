#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct BoxCollider3DComponent {
  Vec3 size = {1.0F, 1.0F, 1.0F};
  Vec3 offset;
  bool isTrigger = false;
  std::string layer;
};

struct SphereCollider3DComponent {
  float radius = 0.5F;
  Vec3 offset;
  bool isTrigger = false;
  std::string layer;
};

struct Rigidbody3DComponent {
  std::string bodyType = "dynamic";
  Vec3 velocity;
  bool useGravity = true;
  float gravityScale = 1.0F;
};

} // namespace demi::runtime
