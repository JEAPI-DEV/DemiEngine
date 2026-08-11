#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
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
};

} // namespace demi::runtime
