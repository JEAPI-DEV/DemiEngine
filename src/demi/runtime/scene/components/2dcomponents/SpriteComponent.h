#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct SpriteComponent {
  static constexpr std::string_view typeName = "Sprite";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string texture;
  std::string shape = "rectangle";
  std::string layer;
  Color color = {1.0F, 1.0F, 1.0F, 1.0F};
};

} // namespace demi::runtime
