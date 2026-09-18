#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include <string>

namespace demi::runtime {
// Authored placement, not an eagerly instantiated prefab. Gameplay decides
// when to instantiate it; editor preview uses the shared prefab resolver.
struct PrefabPlacement3DComponent {
  static constexpr std::string_view typeName = "PrefabPlacement3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor::prefabReference("prefab"),
      ComponentFieldDescriptor{"root", ComponentFieldType::String},
      ComponentFieldDescriptor{"preserve", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"3D", "Prefab Placement 3D"};
  static void parse(const nlohmann::json &json, Entity &entity);
  static nlohmann::json defaults();
  std::string prefab;
  std::string root = "assembly";
  bool preserve = true;
};
} // namespace demi::runtime
