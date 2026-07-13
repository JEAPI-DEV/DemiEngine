#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct Camera2DComponent {
  static constexpr std::string_view typeName = "Camera2D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"clear_color", ComponentFieldType::Color},
      ComponentFieldDescriptor{"orthographic_size", ComponentFieldType::Number},
      ComponentFieldDescriptor{"target", ComponentFieldType::String},
      ComponentFieldDescriptor{"follow_speed", ComponentFieldType::Number},
      ComponentFieldDescriptor{"follow_offset", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"bounds_min", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"bounds_max", ComponentFieldType::Vec2}};
  static constexpr ComponentEditorMetadata editor{"2D", "Camera 2D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  Color clearColor;
  float orthographicSize = 10.0F;
  std::string target;
  float followSpeed = 0.0F;
  Vec2 followOffset;
  Vec2 boundsMin;
  Vec2 boundsMax;
  bool hasBounds = false;
};

} // namespace demi::runtime
