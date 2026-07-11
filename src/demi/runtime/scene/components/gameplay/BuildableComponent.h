#pragma once
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include <string>
namespace demi::runtime {
struct BuildableComponent {
  static constexpr std::string_view typeName = "Buildable";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::Generic;
  static void parse(const nlohmann::json &json, Entity &entity);
  std::string asset;
  bool blocksMovement = false;
};
} // namespace demi::runtime
