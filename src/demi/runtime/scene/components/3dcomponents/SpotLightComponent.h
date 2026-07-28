#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime {

struct SpotLightComponent {
  static constexpr std::string_view typeName = "SpotLight";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"color", ComponentFieldType::Color},
      ComponentFieldDescriptor{"intensity", ComponentFieldType::Number, false,
                               true, {}, 0.0, true},
      ComponentFieldDescriptor{"range", ComponentFieldType::Number, false, true,
                               {}, 0.001, true},
      ComponentFieldDescriptor{"inner_angle", ComponentFieldType::Number,
                               false, true, {}, 0.0, true, false, true, true,
                               false, 179.0, true},
      ComponentFieldDescriptor{"outer_angle", ComponentFieldType::Number,
                               false, true, {}, 0.0, true, false, true, true,
                               false, 179.0, true},
      ComponentFieldDescriptor{"direction", ComponentFieldType::Vec3},
      ComponentFieldDescriptor{"casts_shadows", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"render_mask", ComponentFieldType::String}};
  static constexpr ComponentEditorMetadata editor{"Lighting", "Spot Light"};
  static void parse(const nlohmann::json &json, Entity &entity);

  Color color{1.0F, 0.9F, 0.75F, 1.0F};
  float intensity = 1.0F;
  float range = 12.0F;
  float innerAngle = 25.0F;
  float outerAngle = 40.0F;
  Vec3 direction{0.0F, 0.0F, -1.0F};
  bool castsShadows = false;
  std::string renderMask;
};

} // namespace demi::runtime
