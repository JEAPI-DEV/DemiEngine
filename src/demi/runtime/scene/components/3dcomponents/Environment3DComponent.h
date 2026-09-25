#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime {

struct Environment3DComponent {
  static constexpr std::string_view typeName = "Environment3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array msaaOptions{0,2,4,8,16};
  static constexpr std::array fields{
      ComponentFieldDescriptor{"msaa_samples",ComponentFieldType::Integer,false,true,{},0,true,false,true,true,false,16,true}
          .withIntegerChoices(msaaOptions)
          .withHelp("3D edge anti-aliasing: 0 off, 2, 4 (default), 8 or 16 samples. Device support may lower the request."),
      ComponentFieldDescriptor{"relief_cache_meshes", ComponentFieldType::Integer, false, true, {}, 0, true},
      ComponentFieldDescriptor{"relief_image_cache_mb", ComponentFieldType::Integer, false, true, {}, 0, true},
      ComponentFieldDescriptor::assetReference("sky_texture"),
      ComponentFieldDescriptor{"ambient_color", ComponentFieldType::Color},
      ComponentFieldDescriptor{"ambient_intensity", ComponentFieldType::Number,
                               false, true, {}, 0.0, true},
      ComponentFieldDescriptor{"fog_color", ComponentFieldType::Color},
      ComponentFieldDescriptor{"fog_start", ComponentFieldType::Number, false,
                               true, {}, 0.0, true},
      ComponentFieldDescriptor{"fog_end", ComponentFieldType::Number, false,
                               true, {}, 0.001, true},
      ComponentFieldDescriptor{"shadow_distance", ComponentFieldType::Number,
                               false, true, {}, 0.0, true}.withHelp("Shadow coverage radius in metres. Zero disables the pass."),
      ComponentFieldDescriptor{"shadow_resolution", ComponentFieldType::Integer,
                               false, true, {}, 1.0, true, false, true, true,
                               false, 65535.0, true}.withHelp("Square map resolution. The rendering device's texture limits apply."),
      [] {
        auto field=ComponentFieldDescriptor{"shadow_bias", ComponentFieldType::Number, false, true, {}, 0, true};
        field.editor.help="Depth offset in metres. Large values detach shadows from their casters.";
        field.editor.numericStep=.001;
        return field;
      }(),
      ComponentFieldDescriptor{"max_shadow_lights",
                               ComponentFieldType::Integer, false, true, {},
                               0.0, true, false, true, true, false, 2147483647.0,
                               true}.withHelp("Zero disables shadows. The current renderer supports one directional shadow map per camera.")};
  static constexpr ComponentEditorMetadata editor{"Lighting",
                                                  "3D Environment"};
  static void parse(const nlohmann::json &json, Entity &entity);
  static nlohmann::json defaults();

  Color ambientColor{0.35F, 0.4F, 0.5F, 1.0F};
  int msaaSamples = 4;
  std::string skyTexture;
  std::size_t reliefCacheMeshes = 256;
  std::size_t reliefImageCacheBytes = 64U * 1024U * 1024U;
  float ambientIntensity = 0.5F;
  Color fogColor{0.56F, 0.74F, 0.95F, 1.0F};
  float fogStart = 80.0F;
  float fogEnd = 220.0F;
  float shadowDistance = 80.0F;
  int shadowResolution = 1024;
  float shadowBias = .02F;
  int maxShadowLights = 1;
};

} // namespace demi::runtime
