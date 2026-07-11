#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct BoxCollider2DComponent {
  static constexpr std::string_view typeName = "BoxCollider2D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static void parse(const nlohmann::json &json, Entity &entity);

  Vec2 size = {1.0F, 1.0F};
  Vec2 offset;
  bool isTrigger = false;
  std::string layer;
};

} // namespace demi::runtime
