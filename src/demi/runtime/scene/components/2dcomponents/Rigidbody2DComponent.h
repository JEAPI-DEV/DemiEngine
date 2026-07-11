#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct Rigidbody2DComponent {
  static constexpr std::string_view typeName = "Rigidbody2D";
  static constexpr bool exposedToLua = true;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string bodyType = "dynamic";
  Vec2 velocity;
  float gravityScale = 1.0F;
  float bounciness = 0.0F;
  bool lockRotation = true;
};

} // namespace demi::runtime
