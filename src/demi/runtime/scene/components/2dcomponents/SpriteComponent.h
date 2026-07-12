#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>

namespace demi::runtime {

struct SpriteComponent {
  static constexpr std::string_view typeName = "Sprite";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array<std::string_view, 3> shapes{"rectangle", "circle",
                                                          "triangle"};
  static constexpr std::array fields{
      ComponentFieldDescriptor{"texture", ComponentFieldType::String},
      ComponentFieldDescriptor{"shape", ComponentFieldType::String, false, true,
                               shapes},
      ComponentFieldDescriptor{"layer", ComponentFieldType::String},
      ComponentFieldDescriptor{"color", ComponentFieldType::Color}};
  static constexpr ComponentEditorMetadata editor{"2D", "Sprite"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string texture;
  std::string shape = "rectangle";
  std::string layer;
  Color color = {1.0F, 1.0F, 1.0F, 1.0F};
};

} // namespace demi::runtime
