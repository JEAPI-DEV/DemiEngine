#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct HitboxControllerComponent {
  Vec2 hurtbox = {1.0F, 1.0F};
  std::string script;
};

struct LuaScriptComponent {
  std::string module;
  std::string propertiesJson;
};

struct BuildableComponent {
  std::string asset;
  bool blocksMovement = false;
};

} // namespace demi::runtime
