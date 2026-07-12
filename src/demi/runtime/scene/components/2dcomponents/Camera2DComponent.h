#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime {

struct Camera2DComponent {
  static constexpr std::string_view typeName = "Camera2D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"clear_color", ComponentFieldType::Color},
      ComponentFieldDescriptor{"orthographic_size",
                               ComponentFieldType::Number}};
  static constexpr ComponentEditorMetadata editor{"2D", "Camera 2D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  Color clearColor;
  float orthographicSize = 10.0F;
};

} // namespace demi::runtime
