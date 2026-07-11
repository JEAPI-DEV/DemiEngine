#include "demi/runtime/scene/components/gameplay/LuaScriptComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void LuaScriptComponent::parse(const nlohmann::json &json, Entity &entity) {
  LuaScriptComponent component;
  component.module = scene_loading::stringOr(json, "module");
  if (const auto *properties = scene_loading::objectField(json, "properties"))
    component.propertiesJson = properties->dump();
  entity.luaScript = component;
}
} // namespace demi::runtime
