#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct Rigidbody3DComponent {
  static constexpr std::string_view typeName = "Rigidbody3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array<std::string_view, 3> bodyTypes{
      "dynamic", "static", "kinematic"};
  static constexpr std::array fields{
      ComponentFieldDescriptor{"body_type", ComponentFieldType::String, false,
                               true, bodyTypes},
      ComponentFieldDescriptor{"velocity", ComponentFieldType::Vec3, false,
                               true, {}, 0.0, false, true},
      ComponentFieldDescriptor{"angular_velocity", ComponentFieldType::Vec3},
      ComponentFieldDescriptor{"use_gravity", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"gravity_scale", ComponentFieldType::Number},
      ComponentFieldDescriptor{"mass", ComponentFieldType::Number, false, true,
                               {}, 0.0001, true},
      ComponentFieldDescriptor{"linear_damping", ComponentFieldType::Number,
                               false, true, {}, 0.0, true},
      ComponentFieldDescriptor{"angular_damping", ComponentFieldType::Number,
                               false, true, {}, 0.0, true},
      ComponentFieldDescriptor{"friction", ComponentFieldType::Number, false,
                               true, {}, 0.0, true},
      ComponentFieldDescriptor{"restitution", ComponentFieldType::Number,
                               false, true, {}, 0.0, true, false, true, true,
                               false, 1.0, true},
      ComponentFieldDescriptor{"continuous", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"allow_sleep", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"awake", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"body_enabled", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"interpolate", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"lock_position_x", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"lock_position_y", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"lock_position_z", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"lock_rotation_x", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"lock_rotation_y", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"lock_rotation_z", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Physics 3D", "Rigidbody 3D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string bodyType = "dynamic";
  Vec3 velocity;
  Vec3 angularVelocity;
  bool useGravity = true;
  float gravityScale = 1.0F;
  float mass = 1.0F;
  float linearDamping = 0.05F;
  float angularDamping = 0.05F;
  float friction = 0.5F;
  float restitution = 0.0F;
  bool continuous = false;
  bool allowSleep = true;
  bool awake = true;
  bool bodyEnabled = true;
  bool interpolate = true;
  bool lockPositionX = false;
  bool lockPositionY = false;
  bool lockPositionZ = false;
  bool lockRotationX = false;
  bool lockRotationY = false;
  bool lockRotationZ = false;

  // Runtime command state. These values are intentionally not serialized.
  Vec3 accumulatedForce;
  Vec3 accumulatedImpulse;
  Vec3 accumulatedTorque;
  Vec3 kinematicTargetPosition;
  Vec3 kinematicTargetRotation;
  float kinematicTargetDt = 0.0F;
  bool hasKinematicTarget = false;
};

} // namespace demi::runtime
