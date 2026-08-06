#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime {

struct CharacterController3DComponent {
  static constexpr std::string_view typeName = "CharacterController3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"radius", ComponentFieldType::Number, false,
                               true, {}, 0.001, true},
      ComponentFieldDescriptor{"height", ComponentFieldType::Number, false,
                               true, {}, 0.002, true},
      ComponentFieldDescriptor{"step_height", ComponentFieldType::Number,
                               false, true, {}, 0.0, true},
      ComponentFieldDescriptor{"slope_limit", ComponentFieldType::Number,
                               false, true, {}, 0.0, true, false, true, true,
                               false, 89.9, true},
      ComponentFieldDescriptor{"skin_width", ComponentFieldType::Number, false,
                               true, {}, 0.0001, true},
      ComponentFieldDescriptor{"gravity", ComponentFieldType::Number},
      ComponentFieldDescriptor{"layer", ComponentFieldType::String}};
  static constexpr ComponentEditorMetadata editor{"Physics 3D",
                                                  "Character Controller 3D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  float radius = 0.4F;
  float height = 1.8F;
  float stepHeight = 0.3F;
  float slopeLimit = 50.0F;
  float skinWidth = 0.02F;
  float gravity = -20.0F;
  std::string layer;
  Vec3 velocity;
  Vec3 desiredVelocity;
  float requestedJumpSpeed = 0.0F;
  bool grounded = false;
  std::string groundEntity;
};

} // namespace demi::runtime
