#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>
#include <unordered_map>

namespace demi::runtime {

struct IsoGridComponent {
  static constexpr std::string_view typeName = "IsoGrid";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"cell_size", ComponentFieldType::Vec2},
      ComponentFieldDescriptor{"width", ComponentFieldType::Integer},
      ComponentFieldDescriptor{"height", ComponentFieldType::Integer},
      ComponentFieldDescriptor::assetReference("default_texture"),
      // A dedicated grid-cell adapter presents this compact map in the editor;
      // the generic inspector must not expose it as raw JSON.
      ComponentFieldDescriptor{"cell_textures", ComponentFieldType::Object,
                               false, false}};
  static constexpr ComponentEditorMetadata editor{"Isometric", "Iso Grid"};
  static void parse(const nlohmann::json &json, Entity &entity);

  Vec2 cellSize = {1.0F, 0.5F};
  int width = 0;
  int height = 0;
  std::string defaultTexture;
  std::unordered_map<std::string, std::string> cellTextures;
};

} // namespace demi::runtime
