#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct Rigidbody2DComponent {
  static constexpr std::string_view typeName = "Rigidbody2D";
  static constexpr bool exposedToLua = true;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array<std::string_view, 3> bodyTypes{
      "dynamic", "static", "kinematic"};
  static constexpr std::array fields{
      ComponentFieldDescriptor{"body_type", ComponentFieldType::String, false,
                               true, bodyTypes},
      ComponentFieldDescriptor{"velocity",
                               ComponentFieldType::Vec2,
                               false,
                               true,
                               {},
                               0.0,
                               false,
                               true},
      ComponentFieldDescriptor{"gravity_scale", ComponentFieldType::Number},
      ComponentFieldDescriptor{"bounciness", ComponentFieldType::Number},
      ComponentFieldDescriptor{"lock_rotation", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"angular_velocity", ComponentFieldType::Number},
      ComponentFieldDescriptor{"linear_damping", ComponentFieldType::Number},
      ComponentFieldDescriptor{"angular_damping", ComponentFieldType::Number},
      ComponentFieldDescriptor{"continuous", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"allow_sleep", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"awake", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"body_enabled", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Physics 2D", "Rigidbody 2D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string bodyType = "dynamic";
  Vec2 velocity;
  float gravityScale = 1.0F;
  float bounciness = 0.0F;
  bool lockRotation = true;
  float angularVelocity = 0.0F;
  float linearDamping = 0.0F;
  float angularDamping = 0.0F;
  bool continuous = false;
  bool allowSleep = true;
  bool awake = true;
  bool bodyEnabled = true;
};

} // namespace demi::runtime
