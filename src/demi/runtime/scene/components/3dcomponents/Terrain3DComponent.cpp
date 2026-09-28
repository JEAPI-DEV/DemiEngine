#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include <stdexcept>

namespace demi::runtime {
void Terrain3DComponent::parse(const nlohmann::json &json, Entity &entity) {
  if (!json.is_object())
    throw std::invalid_argument("Terrain3D must be an object");
  Terrain3DComponent value;
  if (json.contains("recipe"))
    value.recipe = json.at("recipe");
  (void)TerrainRecipe::parse(value.recipe);
  entity.setComponent(std::move(value));
}
nlohmann::json Terrain3DComponent::defaults() {
  return {{"recipe", TerrainRecipe::defaults()}};
}
bool Terrain3DComponent::serializeField(const Terrain3DComponent &component,
                                        std::string_view field,
                                        nlohmann::json &out) {
  if (field != "recipe")
    return false;
  out = component.recipe;
  return true;
}
} // namespace demi::runtime
