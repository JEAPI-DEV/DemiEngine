#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime {

struct Environment3DComponent {
  static constexpr std::string_view typeName = "Environment3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"ambient_color", ComponentFieldType::Color},
      ComponentFieldDescriptor{"ambient_intensity", ComponentFieldType::Number,
                               false, true, {}, 0.0, true},
      ComponentFieldDescriptor{"fog_color", ComponentFieldType::Color},
      ComponentFieldDescriptor{"fog_start", ComponentFieldType::Number, false,
                               true, {}, 0.0, true},
      ComponentFieldDescriptor{"fog_end", ComponentFieldType::Number, false,
                               true, {}, 0.001, true},
      ComponentFieldDescriptor{"shadow_distance", ComponentFieldType::Number,
                               false, true, {}, 0.0, true},
      ComponentFieldDescriptor{"shadow_resolution", ComponentFieldType::Integer,
                               false, true, {}, 128.0, true, false, true, true,
                               false, 4096.0, true},
      ComponentFieldDescriptor{"max_shadow_lights",
                               ComponentFieldType::Integer, false, true, {},
                               0.0, true, false, true, true, false, 4.0,
                               true}};
  static constexpr ComponentEditorMetadata editor{"Lighting",
                                                  "3D Environment"};
  static void parse(const nlohmann::json &json, Entity &entity);

  Color ambientColor{0.35F, 0.4F, 0.5F, 1.0F};
  float ambientIntensity = 0.5F;
  Color fogColor{0.56F, 0.74F, 0.95F, 1.0F};
  float fogStart = 80.0F;
  float fogEnd = 220.0F;
  float shadowDistance = 80.0F;
  int shadowResolution = 1024;
  int maxShadowLights = 1;
};

} // namespace demi::runtime
