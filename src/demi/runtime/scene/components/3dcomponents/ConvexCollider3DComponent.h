#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>
#include <vector>

namespace demi::runtime {

struct ConvexCollider3DComponent {
  static constexpr std::string_view typeName = "ConvexCollider3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"points", ComponentFieldType::Vec3Array, true},
      ComponentFieldDescriptor{"offset", ComponentFieldType::Vec3},
      ComponentFieldDescriptor{"is_trigger", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"layer", ComponentFieldType::String},
      ComponentFieldDescriptor{"debug_visible", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Physics 3D",
                                                  "Convex Collider 3D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::vector<Vec3> points;
  Vec3 offset;
  bool isTrigger = false;
  std::string layer;
  bool debugVisible = false;
};

} // namespace demi::runtime
