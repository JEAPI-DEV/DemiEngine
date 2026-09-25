#pragma once
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include <optional>
#include <string>

namespace demi::runtime {
struct Fracture3DComponent {
  static constexpr std::array<std::string_view, 2> colliderValues{"source", "box"};
  static constexpr std::string_view typeName = "Fracture3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"collider", ComponentFieldType::String, false,
                               true, colliderValues},
      ComponentFieldDescriptor{"pieces",
                               ComponentFieldType::Integer,
                               false,
                               true,
                               {},
                               1,
                               true,
                               false,
                               true,
                               true,
                               false,
                               0,
                               false},
      ComponentFieldDescriptor{"bond_health",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               0.000001,
                               true,
                               false,
                               true,
                               true,
                               false,
                               1e30,
                               true},
      ComponentFieldDescriptor{"anchor_below",
                               ComponentFieldType::Number,
                               false,
                               true,
                               {},
                               0,
                               false,
                               false,
                               true,
                               true,
                               true},
      ComponentFieldDescriptor{"density", ComponentFieldType::Number, false,
                               true, {}, 0.001, true, false, true, true, true,
                               1000000, true},
      ComponentFieldDescriptor{"interior_color", ComponentFieldType::Color},
      ComponentFieldDescriptor::assetReference("interior_material"),
      ComponentFieldDescriptor{"debris_lifetime", ComponentFieldType::Number, false, true, {}, 0, true}.withHelp("Zero keeps physical rubble. Positive seconds makes detached chunks non-colliding and fades them before removal. Mixed chunks stay physical."),
      ComponentFieldDescriptor{"debris_fade", ComponentFieldType::Number, false, true, {}, 0, true}.withHelp("Fade duration at the end of debris lifetime, in seconds.")};
  static constexpr ComponentEditorMetadata editor{"Physics 3D", "Fracture 3D"};
  static void parse(const nlohmann::json &, Entity &);
  static nlohmann::json defaults();
  int pieces = 8;
  std::string collider = "source";
  float bondHealth = 1;
  std::optional<float> anchorBelow;
  std::optional<float> density;
  std::array<float, 4> interiorColor{0.35F, 0.33F, 0.30F, 1};
  std::string interiorMaterial;
  float debrisLifetime=0, debrisFade=1;
};
} // namespace demi::runtime
