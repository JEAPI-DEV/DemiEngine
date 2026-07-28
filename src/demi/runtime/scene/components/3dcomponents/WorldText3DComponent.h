#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

namespace demi::runtime {

struct WorldText3DComponent {
  static constexpr std::string_view typeName = "WorldText3D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::ThreeDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"text", ComponentFieldType::String},
      ComponentFieldDescriptor::assetReference("font"),
      ComponentFieldDescriptor{"color", ComponentFieldType::Color},
      ComponentFieldDescriptor{"font_size", ComponentFieldType::Number, false,
                               true, {}, 0.01, true},
      ComponentFieldDescriptor{"billboard", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"max_distance", ComponentFieldType::Number,
                               false, true, {}, 0.0, true},
      ComponentFieldDescriptor{"render_mask", ComponentFieldType::String}};
  static constexpr ComponentEditorMetadata editor{"3D", "World Text"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string text;
  std::string font;
  Color color{1.0F, 1.0F, 1.0F, 1.0F};
  float fontSize = 1.0F;
  bool billboard = true;
  float maxDistance = 100.0F;
  std::string renderMask;
};

} // namespace demi::runtime
