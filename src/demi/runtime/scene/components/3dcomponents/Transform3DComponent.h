#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct Transform3DComponent {
  static constexpr std::string_view typeName = "Transform3D";
  static constexpr bool exposedToLua = true;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"parent", ComponentFieldType::String},
      ComponentFieldDescriptor{"position", ComponentFieldType::Vec3},
      ComponentFieldDescriptor{"rotation", ComponentFieldType::Vec3},
      ComponentFieldDescriptor{"scale", ComponentFieldType::Vec3}};
  static constexpr ComponentEditorMetadata editor{"3D", "Transform 3D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string parent;
  Vec3 position;
  Vec3 rotation;
  Vec3 scale = {1.0F, 1.0F, 1.0F};
};

} // namespace demi::runtime
