#include "demi/runtime/scene/components/gameplay/LuaScriptComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
bool LuaScriptComponent::serializeField(const LuaScriptComponent &component,
                                        std::string_view field,
                                        nlohmann::json &out) {
  if (field != "properties")
    return false;
  out = component.propertiesJson.empty()
            ? nlohmann::json::object()
            : nlohmann::json::parse(component.propertiesJson);
  return true;
}

void LuaScriptComponent::parse(const nlohmann::json &json, Entity &entity) {
  LuaScriptComponent component;
  component.module = scene_loading::stringOr(json, "module");
  if (const auto *properties = scene_loading::objectField(json, "properties"))
    component.propertiesJson = properties->dump();
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
