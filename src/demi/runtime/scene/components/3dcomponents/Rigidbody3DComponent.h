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
      ComponentFieldDescriptor{"velocity", ComponentFieldType::Vec3},
      ComponentFieldDescriptor{"use_gravity", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"gravity_scale", ComponentFieldType::Number}};
  static constexpr ComponentEditorMetadata editor{"Physics 3D", "Rigidbody 3D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string bodyType = "dynamic";
  Vec3 velocity;
  bool useGravity = true;
  float gravityScale = 1.0F;
};

} // namespace demi::runtime
