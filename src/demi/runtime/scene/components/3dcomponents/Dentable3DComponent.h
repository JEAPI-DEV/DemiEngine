#pragma once

#include "demi/runtime/geometry/MeshImpact3D.h"
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include <vector>

namespace demi::runtime {
struct Dentable3DComponent {
  static constexpr std::string_view typeName = "Dentable3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"radius",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               0.000001,
                               true},
      ComponentFieldDescriptor{"yield_energy",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               0.0,
                               true},
      ComponentFieldDescriptor{"stiffness",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               0.000001,
                               true},
      ComponentFieldDescriptor{"absorption",
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
      ComponentFieldDescriptor{"max_depth",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               0.000001,
                               true}};
  static constexpr ComponentEditorMetadata editor{"3D", "Dentable 3D"};
  static void parse(const nlohmann::json &json, Entity &entity);
  static nlohmann::json defaults();

  MeshImpactMaterial3D material;
  // Only opted-in entities allocate dent state. Never serialized.
  std::vector<MeshDent3D> dents;
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<Dentable3DComponent>::nestedMember<
          &Dentable3DComponent::material, &MeshImpactMaterial3D::radius>("radius"),
      RuntimeFieldBinding<Dentable3DComponent>::nestedMember<
          &Dentable3DComponent::material, &MeshImpactMaterial3D::yieldEnergy>("yield_energy"),
      RuntimeFieldBinding<Dentable3DComponent>::nestedMember<
          &Dentable3DComponent::material, &MeshImpactMaterial3D::stiffness>("stiffness"),
      RuntimeFieldBinding<Dentable3DComponent>::nestedMember<
          &Dentable3DComponent::material, &MeshImpactMaterial3D::absorption>("absorption"),
      RuntimeFieldBinding<Dentable3DComponent>::nestedMember<
          &Dentable3DComponent::material, &MeshImpactMaterial3D::maximumDepth>("max_depth")};
};
} // namespace demi::runtime
