#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime {

struct Camera3DComponent {
  static constexpr std::string_view typeName = "Camera3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static void parse(const nlohmann::json &json, Entity &entity);

  Color clearColor;
  float fov = 60.0F;
  float orthographicSize = 10.0F;
  Vec3 targetOffset = {0.0F, 0.0F, 1.0F};
  bool perspective = true;
  float positionX = 0.0F;
  float upAxis = 1.0F;
};

} // namespace demi::runtime
