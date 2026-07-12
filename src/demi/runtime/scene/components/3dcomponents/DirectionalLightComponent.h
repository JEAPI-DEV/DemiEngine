#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime {

struct DirectionalLightComponent {
  static constexpr std::string_view typeName = "DirectionalLight";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"direction", ComponentFieldType::Vec3},
      ComponentFieldDescriptor{"color", ComponentFieldType::Color},
      ComponentFieldDescriptor{"intensity", ComponentFieldType::Number}};
  static constexpr ComponentEditorMetadata editor{"Lighting",
                                                  "Directional Light"};
  static void parse(const nlohmann::json &json, Entity &entity);

  Vec3 direction = {-0.4F, -1.0F, -0.3F};
  Color color = {1.0F, 1.0F, 0.95F, 1.0F};
  float intensity = 1.0F;
};

} // namespace demi::runtime
