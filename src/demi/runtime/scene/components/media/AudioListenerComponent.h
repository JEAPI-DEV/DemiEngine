#pragma once
#include "demi/runtime/scene/components/ComponentDefinition.h"
namespace demi::runtime {
struct AudioListenerComponent {
  static constexpr std::string_view typeName = "AudioListener";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::Generic;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"primary", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Media", "Audio Listener"};
  static void parse(const nlohmann::json &json, Entity &entity);
  bool primary = true;
};
} // namespace demi::runtime
