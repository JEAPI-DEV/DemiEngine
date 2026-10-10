#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime {

struct DirectionalLightComponent {
  static constexpr std::string_view typeName = "DirectionalLight";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"direction", ComponentFieldType::Vec3},
      ComponentFieldDescriptor{"color", ComponentFieldType::Color},
      ComponentFieldDescriptor{"intensity", ComponentFieldType::Number, false,
                               true, {}, 0.0, true},
      ComponentFieldDescriptor{"casts_shadows", ComponentFieldType::Boolean}.withHelp("Enable cascaded shadows for the primary sun."),
      ComponentFieldDescriptor{"render_mask", ComponentFieldType::String}.asAdvanced().withHelp("Optional camera mask match. Empty affects every camera.")};
  static constexpr ComponentEditorMetadata editor{"Lighting", "Sun Light",
      "Lights the whole scene from one direction. Rotate its Transform gizmo to aim it; moving a sun does not change illumination. Only the primary sun supplies cascaded shadows."};
  static void parse(const nlohmann::json &json, Entity &entity);

  Vec3 direction = {-0.4F, -1.0F, -0.3F};
  Color color = {1.0F, 1.0F, 0.95F, 1.0F};
  float intensity = 1.0F;
  bool castsShadows = false;
  std::string renderMask;
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<DirectionalLightComponent>::member<
          &DirectionalLightComponent::direction>("direction"),
      RuntimeFieldBinding<DirectionalLightComponent>::member<
          &DirectionalLightComponent::color>("color"),
      RuntimeFieldBinding<DirectionalLightComponent>::member<
          &DirectionalLightComponent::intensity>("intensity"),
      RuntimeFieldBinding<DirectionalLightComponent>::member<
          &DirectionalLightComponent::castsShadows>("casts_shadows"),
      RuntimeFieldBinding<DirectionalLightComponent>::member<
          &DirectionalLightComponent::renderMask>("render_mask")};
};

} // namespace demi::runtime
