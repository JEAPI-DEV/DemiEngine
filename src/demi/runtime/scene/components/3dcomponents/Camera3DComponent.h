#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime {

struct Camera3DComponent {
  static constexpr std::string_view typeName = "Camera3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array<std::string_view, 3> clearModes{
      "color", "depth", "none"};
  static constexpr std::array<std::string_view, 9> debugModes{
      "shaded", "normals", "uv", "alpha", "lighting", "bounds",
      "colliders", "overdraw", "instancing"};
  static constexpr std::array fields{
      ComponentFieldDescriptor{"clear_color", ComponentFieldType::Color},
      ComponentFieldDescriptor{"fov", ComponentFieldType::Number},
      ComponentFieldDescriptor{"near_clip", ComponentFieldType::Number},
      ComponentFieldDescriptor{"far_clip", ComponentFieldType::Number},
      ComponentFieldDescriptor{"orthographic_size", ComponentFieldType::Number},
      ComponentFieldDescriptor{"target_offset", ComponentFieldType::Vec3},
      ComponentFieldDescriptor{"perspective", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"position_x", ComponentFieldType::Number},
      ComponentFieldDescriptor{"up_axis", ComponentFieldType::Number},
      ComponentFieldDescriptor{"viewport_x", ComponentFieldType::Number, false,
                               true, {}, 0.0, true, false, true, true, false,
                               1.0, true},
      ComponentFieldDescriptor{"viewport_y", ComponentFieldType::Number, false,
                               true, {}, 0.0, true, false, true, true, false,
                               1.0, true},
      ComponentFieldDescriptor{"viewport_width", ComponentFieldType::Number,
                               false, true, {}, 0.0, true, false, true, true,
                               false, 1.0, true},
      ComponentFieldDescriptor{"viewport_height", ComponentFieldType::Number,
                               false, true, {}, 0.0, true, false, true, true,
                               false, 1.0, true},
      ComponentFieldDescriptor{"render_scale", ComponentFieldType::Number,
                               false, true, {}, 0.25, true, false, true, true,
                               false, 2.0, true},
      ComponentFieldDescriptor{"priority", ComponentFieldType::Integer},
      ComponentFieldDescriptor{"primary", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"render_mask", ComponentFieldType::String},
      ComponentFieldDescriptor{"clear_mode", ComponentFieldType::String, false,
                               true, clearModes},
      ComponentFieldDescriptor{"debug_mode", ComponentFieldType::String,
                               false, true, debugModes},
      ComponentFieldDescriptor::assetReference("render_target"),
      ComponentFieldDescriptor{"update_interval", ComponentFieldType::Number,
                               false, true, {}, 0.0, true},
      ComponentFieldDescriptor{"render_hud_to_target",
                               ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"render_hud", ComponentFieldType::Boolean}};
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
  float viewportX = 0.0F;
  float viewportY = 0.0F;
  float viewportWidth = 1.0F;
  float viewportHeight = 1.0F;
  float renderScale = 1.0F;
  int priority = 0;
  bool primary = false;
  std::string renderMask;
  std::string clearMode = "color";
  std::string debugMode = "shaded";
  std::string renderTarget;
  float updateInterval = 0.0F;
  bool renderHudToTarget = false;
  bool renderHud = true;
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::clearColor>("clear_color"),
      RuntimeFieldBinding<Camera3DComponent>::member<&Camera3DComponent::fov>(
          "fov"),
      RuntimeFieldBinding<Camera3DComponent>::memberWithDerived<
          &Camera3DComponent::nearClip, &Camera3DComponent::farClip>("near_clip"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::farClip>("far_clip"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::orthographicSize>("orthographic_size"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::targetOffset>("target_offset"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::perspective>("perspective"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::positionX>("position_x"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::upAxis>("up_axis"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::viewportX>("viewport_x"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::viewportY>("viewport_y"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::viewportWidth>("viewport_width"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::viewportHeight>("viewport_height"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::renderScale>("render_scale"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::priority>("priority"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::primary>("primary"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::renderMask>("render_mask"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::clearMode>("clear_mode"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::debugMode>("debug_mode"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::renderTarget>("render_target"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::updateInterval>("update_interval"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::renderHudToTarget>("render_hud_to_target"),
      RuntimeFieldBinding<Camera3DComponent>::member<
          &Camera3DComponent::renderHud>("render_hud")};
};

} // namespace demi::runtime
