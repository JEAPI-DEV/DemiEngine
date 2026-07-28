#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime {

struct PostProcessStackComponent {
  static constexpr std::string_view typeName = "PostProcessStack";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"exposure", ComponentFieldType::Number},
      ComponentFieldDescriptor{"contrast", ComponentFieldType::Number, false,
                               true, {}, 0.0, true},
      ComponentFieldDescriptor{"saturation", ComponentFieldType::Number, false,
                               true, {}, 0.0, true},
      ComponentFieldDescriptor{"tint", ComponentFieldType::Color},
      ComponentFieldDescriptor{"vignette", ComponentFieldType::Number, false,
                               true, {}, 0.0, true, false, true, true, false,
                               1.0, true},
      ComponentFieldDescriptor{"bloom", ComponentFieldType::Number, false, true,
                               {}, 0.0, true, false, true, true, false, 2.0,
                               true},
      ComponentFieldDescriptor{"bloom_threshold", ComponentFieldType::Number,
                               false, true, {}, 0.0, true},
      ComponentFieldDescriptor{"fade_color", ComponentFieldType::Color},
      ComponentFieldDescriptor{"fade", ComponentFieldType::Number, false, true,
                               {}, 0.0, true, false, true, true, false, 1.0,
                               true}};
  static constexpr ComponentEditorMetadata editor{"Effects",
                                                  "Post Process Stack"};
  static void parse(const nlohmann::json &json, Entity &entity);

  float exposure = 0.0F;
  float contrast = 1.0F;
  float saturation = 1.0F;
  Color tint{1.0F, 1.0F, 1.0F, 1.0F};
  float vignette = 0.0F;
  float bloom = 0.0F;
  float bloomThreshold = 1.0F;
  Color fadeColor{0.0F, 0.0F, 0.0F, 1.0F};
  float fade = 0.0F;
};

} // namespace demi::runtime
