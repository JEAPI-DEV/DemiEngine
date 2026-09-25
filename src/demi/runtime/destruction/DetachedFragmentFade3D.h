#pragma once
#include "demi/runtime/scene/model/SceneTypes.h"
namespace demi::runtime {
struct World;
// Runtime-only state on the former fragment body and its real visual children.
struct DetachedFragmentFade3D {
  float age = 0, lifetime = 3, fade = 1;
  Vec3 velocity, angularVelocity, center, localCenter;
};
struct FragmentOpacity3D {
  float value = 1;
  std::string body;
};
void updateDetachedFragmentFades3D(World &, float dt, Vec3 gravity);
} // namespace demi::runtime
