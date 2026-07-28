#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct Joint2DComponent {
  static constexpr std::string_view typeName = "Joint2D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array<std::string_view, 5> types{
      "revolute", "prismatic", "weld", "rope", "motor"};
  static constexpr std::array fields{
      ComponentFieldDescriptor{"type", ComponentFieldType::String, true, true,
                               types},
      ComponentFieldDescriptor::entityReference("other_entity", true),
      ComponentFieldDescriptor{"anchor", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"other_anchor", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"axis", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"lower_limit", ComponentFieldType::Number},
      ComponentFieldDescriptor{"upper_limit", ComponentFieldType::Number},
      ComponentFieldDescriptor{"enable_limit", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"motor_speed", ComponentFieldType::Number},
      ComponentFieldDescriptor{"max_motor_force", ComponentFieldType::Number},
      ComponentFieldDescriptor{"max_motor_torque", ComponentFieldType::Number},
      ComponentFieldDescriptor{"enable_motor", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"max_length", ComponentFieldType::Number},
      ComponentFieldDescriptor{"correction_factor", ComponentFieldType::Number},
      ComponentFieldDescriptor{"collide_connected",
                               ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Physics 2D", "Joint 2D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string type = "revolute";
  std::string otherEntity;
  Vec2 anchor;
  Vec2 otherAnchor;
  Vec2 axis = {1.0F, 0.0F};
  float lowerLimit = 0.0F;
  float upperLimit = 0.0F;
  bool enableLimit = false;
  float motorSpeed = 0.0F;
  float maxMotorForce = 0.0F;
  float maxMotorTorque = 0.0F;
  bool enableMotor = false;
  float maxLength = 1.0F;
  float correctionFactor = 0.3F;
  bool collideConnected = false;
};

} // namespace demi::runtime
