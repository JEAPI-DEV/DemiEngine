#pragma once
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include <string>
namespace demi::runtime {
struct LuaScriptComponent {
  static constexpr std::string_view typeName = "LuaScript";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::Generic;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"module", ComponentFieldType::String, true},
      ComponentFieldDescriptor{"properties", ComponentFieldType::Object}};
  static constexpr ComponentEditorMetadata editor{"Scripting", "Lua Script"};
  static void parse(const nlohmann::json &json, Entity &entity);
  static bool serializeField(const LuaScriptComponent &component,
                             std::string_view field, nlohmann::json &out);
  std::string module;
  std::string propertiesJson;
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<LuaScriptComponent>::member<
          &LuaScriptComponent::module>("module"),
      RuntimeFieldBinding<LuaScriptComponent>::member<
          &LuaScriptComponent::propertiesJson>("properties")};
};
} // namespace demi::runtime
