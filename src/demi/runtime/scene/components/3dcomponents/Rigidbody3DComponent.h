#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
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
      ComponentFieldDescriptor{"velocity",
                               ComponentFieldType::Vec3,
                               false,
                               true,
                               {},
                               0.0,
                               false,
                               true},
      ComponentFieldDescriptor{"angular_velocity", ComponentFieldType::Vec3},
      ComponentFieldDescriptor{"use_gravity", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"gravity_scale", ComponentFieldType::Number},
      ComponentFieldDescriptor{
          "mass", ComponentFieldType::Number, false, true, {}, 0.0001, true},
      ComponentFieldDescriptor{"linear_damping",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               0.0,
                               true},
      ComponentFieldDescriptor{"angular_damping",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               0.0,
                               true},
      ComponentFieldDescriptor{
          "friction", ComponentFieldType::Number, false, true, {}, 0.0, true},
      ComponentFieldDescriptor{"restitution",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               0.0,
                               true,
                               false,
                               true,
                               true,
                               false,
                               1.0,
                               true},
      ComponentFieldDescriptor{"continuous", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"solver_velocity_steps",
                               ComponentFieldType::Integer,
                               false,
                               true,
                               {},
                               0.0,
                               true,
                               false,
                               true,
                               true,
                               false,
                               128.0,
                               true},
      ComponentFieldDescriptor{"solver_position_steps",
                               ComponentFieldType::Integer,
                               false,
                               true,
                               {},
                               0.0,
                               true,
                               false,
                               true,
                               true,
                               false,
                               128.0,
                               true},
      ComponentFieldDescriptor{"report_contacts", ComponentFieldType::Boolean},
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
  int solverVelocitySteps = 0;
  int solverPositionSteps = 0;
  float friction = 0.5F;
  float restitution = 0.0F;
  bool continuous = false;
  bool reportContacts = true;
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
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::bodyType>("body_type"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::velocity>("velocity"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::angularVelocity>("angular_velocity"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::useGravity>("use_gravity"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::gravityScale>("gravity_scale"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::mass>("mass"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::linearDamping>("linear_damping"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::angularDamping>("angular_damping"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::solverVelocitySteps>("solver_velocity_steps"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::solverPositionSteps>("solver_position_steps"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::friction>("friction"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::restitution>("restitution"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::continuous>("continuous"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::reportContacts>("report_contacts"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::allowSleep>("allow_sleep"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::awake>("awake"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::bodyEnabled>("body_enabled"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::interpolate>("interpolate"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::lockPositionX>("lock_position_x"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::lockPositionY>("lock_position_y"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::lockPositionZ>("lock_position_z"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::lockRotationX>("lock_rotation_x"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::lockRotationY>("lock_rotation_y"),
      RuntimeFieldBinding<Rigidbody3DComponent>::member<
          &Rigidbody3DComponent::lockRotationZ>("lock_rotation_z")};
};

} // namespace demi::runtime
