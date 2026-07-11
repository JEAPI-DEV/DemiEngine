#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime {

struct IsoGridComponent {
  static constexpr std::string_view typeName = "IsoGrid";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static void parse(const nlohmann::json &json, Entity &entity);

  Vec2 cellSize = {1.0F, 0.5F};
  int width = 0;
  int height = 0;
};

} // namespace demi::runtime
