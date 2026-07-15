#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"

#include <string>

namespace demi::runtime {

struct ModelCollider3DComponent {
  static constexpr std::string_view typeName = "ModelCollider3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"asset", ComponentFieldType::String, true},
      ComponentFieldDescriptor{"is_trigger", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"layer", ComponentFieldType::String}};
  static constexpr ComponentEditorMetadata editor{"Physics 3D",
                                                  "Model Collider 3D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string asset;
  bool isTrigger = false;
  std::string layer;
};

} // namespace demi::runtime
