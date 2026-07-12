#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"

#include <string>

namespace demi::runtime {

struct GameplayDataComponent {
  static constexpr std::string_view typeName = "GameplayData";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::Generic;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"values", ComponentFieldType::Object, true}};
  static constexpr ComponentEditorMetadata editor{"Gameplay", "Gameplay Data"};

  static void parse(const nlohmann::json &json, Entity &entity);

  std::string valuesJson = "{}";
};

} // namespace demi::runtime
