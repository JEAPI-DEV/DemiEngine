#pragma once
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include <string>
namespace demi::runtime {
struct BuildableComponent {
  static constexpr std::string_view typeName = "Buildable";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::Generic;
  static constexpr std::array fields{
      ComponentFieldDescriptor::assetReference("asset"),
      ComponentFieldDescriptor{"blocks_movement", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Gameplay", "Buildable"};
  static void parse(const nlohmann::json &json, Entity &entity);
  std::string asset;
  bool blocksMovement = false;
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<BuildableComponent>::member<
          &BuildableComponent::asset>("asset"),
      RuntimeFieldBinding<BuildableComponent>::member<
          &BuildableComponent::blocksMovement>("blocks_movement")};
};
} // namespace demi::runtime
