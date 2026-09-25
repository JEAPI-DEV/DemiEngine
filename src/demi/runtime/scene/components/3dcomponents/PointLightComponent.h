#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime {

struct PointLightComponent {
  static constexpr std::string_view typeName = "PointLight";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"color", ComponentFieldType::Color},
      ComponentFieldDescriptor{"intensity", ComponentFieldType::Number, false,
                               true, {}, 0.0, true},
      ComponentFieldDescriptor{"range", ComponentFieldType::Number, false, true,
                               {}, 0.001, true},
      ComponentFieldDescriptor{"casts_shadows", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"render_mask", ComponentFieldType::String}};
  static constexpr ComponentEditorMetadata editor{"Lighting", "Point Light"};
  static void parse(const nlohmann::json &json, Entity &entity);

  Color color{1.0F, 0.85F, 0.65F, 1.0F};
  float intensity = 1.0F;
  float range = 8.0F;
  bool castsShadows = false;
  std::string renderMask;
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<PointLightComponent>::member<
          &PointLightComponent::color>("color"),
      RuntimeFieldBinding<PointLightComponent>::member<
          &PointLightComponent::intensity>("intensity"),
      RuntimeFieldBinding<PointLightComponent>::member<
          &PointLightComponent::range>("range"),
      RuntimeFieldBinding<PointLightComponent>::member<
          &PointLightComponent::castsShadows>("casts_shadows"),
      RuntimeFieldBinding<PointLightComponent>::member<
          &PointLightComponent::renderMask>("render_mask")};
};

} // namespace demi::runtime
