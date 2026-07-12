#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime {

struct IsoTransformComponent {
  static constexpr std::string_view typeName = "IsoTransform";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"tile", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"height", ComponentFieldType::Number},
      ComponentFieldDescriptor{"footprint", ComponentFieldType::Vec2}};
  static constexpr ComponentEditorMetadata editor{"Isometric", "Iso Transform"};
  static void parse(const nlohmann::json &json, Entity &entity);

  Vec2 tile;
  float height = 0.0F;
  Vec2 footprint = {1.0F, 1.0F};
};

} // namespace demi::runtime
