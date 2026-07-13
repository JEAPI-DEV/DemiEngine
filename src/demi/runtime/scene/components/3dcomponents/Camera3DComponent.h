#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime {

struct Camera3DComponent {
  static constexpr std::string_view typeName = "Camera3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"clear_color", ComponentFieldType::Color},
      ComponentFieldDescriptor{"fov", ComponentFieldType::Number},
      ComponentFieldDescriptor{"near_clip", ComponentFieldType::Number},
      ComponentFieldDescriptor{"far_clip", ComponentFieldType::Number},
      ComponentFieldDescriptor{"orthographic_size", ComponentFieldType::Number},
      ComponentFieldDescriptor{"target_offset", ComponentFieldType::Vec3},
      ComponentFieldDescriptor{"perspective", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"position_x", ComponentFieldType::Number},
      ComponentFieldDescriptor{"up_axis", ComponentFieldType::Number}};
  static constexpr ComponentEditorMetadata editor{"3D", "Camera 3D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  Color clearColor;
  float fov = 60.0F;
  float nearClip = 0.05F;
  float farClip = 500.0F;
  float orthographicSize = 10.0F;
  Vec3 targetOffset = {0.0F, 0.0F, 1.0F};
  bool perspective = true;
  float positionX = 0.0F;
  float upAxis = 1.0F;
};

} // namespace demi::runtime
