#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime {

struct Tilemap2DComponent {
  static constexpr std::string_view typeName = "Tilemap2D";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::TwoDimensional;
  static constexpr std::array fields{
      ComponentFieldDescriptor::assetReference("asset"),
      ComponentFieldDescriptor{"pixels_per_unit", ComponentFieldType::Number},
      ComponentFieldDescriptor{"layer", ComponentFieldType::String},
      ComponentFieldDescriptor{"sorting_order", ComponentFieldType::Integer}};
  static constexpr ComponentEditorMetadata editor{"2D", "Tilemap 2D"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::string asset;
  float pixelsPerUnit = 16.0F;
  std::string layer;
  int sortingOrder = 0;
  std::unordered_map<std::string, int> tileOverrides;
  std::unordered_set<std::string> dirtyChunks;
};

} // namespace demi::runtime
