#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct CapsuleCollider3DComponent {
  static constexpr std::string_view typeName = "CapsuleCollider3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"radius", ComponentFieldType::Number, false,
                               true, {}, 0.001, true},
      ComponentFieldDescriptor{"height", ComponentFieldType::Number, false,
                               true, {}, 0.002, true},
      ComponentFieldDescriptor{"offset", ComponentFieldType::Vec3},
      ComponentFieldDescriptor{"is_trigger", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"layer", ComponentFieldType::String},
      ComponentFieldDescriptor{"debug_visible", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Physics 3D",
                                                  "Capsule Collider 3D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  float radius = 0.5F;
  float height = 2.0F;
  Vec3 offset;
  bool isTrigger = false;
  std::string layer;
  bool debugVisible = false;
};

} // namespace demi::runtime
