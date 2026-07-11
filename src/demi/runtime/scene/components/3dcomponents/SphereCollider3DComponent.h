#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct SphereCollider3DComponent {
  static constexpr std::string_view typeName = "SphereCollider3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static void parse(const nlohmann::json &json, Entity &entity);

  float radius = 0.5F;
  Vec3 offset;
  bool isTrigger = false;
  std::string layer;
};

} // namespace demi::runtime
