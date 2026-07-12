#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct Transform2DComponent {
  static constexpr std::string_view typeName = "Transform2D";
  static constexpr bool exposedToLua = true;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"parent", ComponentFieldType::String},
      ComponentFieldDescriptor{"position", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"rotation", ComponentFieldType::Number},
      ComponentFieldDescriptor{"scale", ComponentFieldType::Vec2}};
  static constexpr ComponentEditorMetadata editor{"2D", "Transform 2D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string parent;
  Vec2 position;
  float rotation = 0.0F;
  Vec2 scale = {1.0F, 1.0F};
};

} // namespace demi::runtime
