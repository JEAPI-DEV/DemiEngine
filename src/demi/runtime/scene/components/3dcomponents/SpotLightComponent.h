#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
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
      ComponentFieldDescriptor{"casts_shadows", ComponentFieldType::Boolean}.asAdvanced().withHelp("Reserved for local-light shadows; currently not rendered."),
      ComponentFieldDescriptor{"render_mask", ComponentFieldType::String}.asAdvanced().withHelp("Optional camera mask match. Empty affects every camera.")};
  static constexpr ComponentEditorMetadata editor{"Lighting", "Spot Light",
      "Emits a cone from its Transform position. Move and rotate it with the gizmos; cone angles and range shape the beam. Local-light shadows are not implemented."};
  static void parse(const nlohmann::json &json, Entity &entity);

  Color color{1.0F, 0.9F, 0.75F, 1.0F};
  float intensity = 1.0F;
  float range = 12.0F;
  float innerAngle = 25.0F;
  float outerAngle = 40.0F;
  Vec3 direction{0.0F, 0.0F, -1.0F};
  bool castsShadows = false;
  std::string renderMask;
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<SpotLightComponent>::member<
          &SpotLightComponent::color>("color"),
      RuntimeFieldBinding<SpotLightComponent>::member<
          &SpotLightComponent::intensity>("intensity"),
      RuntimeFieldBinding<SpotLightComponent>::member<
          &SpotLightComponent::range>("range"),
      RuntimeFieldBinding<SpotLightComponent>::memberWithDerived<
          &SpotLightComponent::innerAngle,
          &SpotLightComponent::outerAngle>("inner_angle"),
      RuntimeFieldBinding<SpotLightComponent>::member<
          &SpotLightComponent::outerAngle>("outer_angle"),
      RuntimeFieldBinding<SpotLightComponent>::member<
          &SpotLightComponent::direction>("direction"),
      RuntimeFieldBinding<SpotLightComponent>::member<
          &SpotLightComponent::castsShadows>("casts_shadows"),
      RuntimeFieldBinding<SpotLightComponent>::member<
          &SpotLightComponent::renderMask>("render_mask")};
};

} // namespace demi::runtime
