#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"

namespace demi::runtime {

struct AnimationPlayer3DComponent {
  static constexpr std::string_view typeName = "AnimationPlayer3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"clip", ComponentFieldType::Integer},
      ComponentFieldDescriptor{"speed", ComponentFieldType::Number},
      ComponentFieldDescriptor{"time", ComponentFieldType::Number},
      ComponentFieldDescriptor{"loop", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"playing", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Animation",
                                                  "Animation Player 3D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  int clip = 0;
  float speed = 1.0F;
  float time = 0.0F;
  bool loop = true;
  bool playing = true;
};

} // namespace demi::runtime
