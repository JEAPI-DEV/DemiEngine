#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include <stdexcept>

namespace demi::runtime {
bool Terrain3DComponent::validateAuthored(const nlohmann::json &json,
                                          std::string &error) {
  if (!json.is_object() ||
      (json.contains("asset") && !json.at("asset").is_string())) {
    error = "Terrain3D requires an object and a string asset reference";
    return false;
  }
  const auto asset = json.value("asset", std::string{});
  if (!asset.empty() && !asset.starts_with("asset://")) {
    error = "Terrain3D asset requires an asset:// reference";
    return false;
  }
  if (!asset.empty() && json.contains("recipe")) {
    error =
        "Terrain3D accepts an asset or an inline procedural recipe, not both";
    return false;
  }
  if (json.contains("recipe")) {
    try {
      (void)TerrainRecipe::parse(json.at("recipe"));
    } catch (const std::exception &failure) {
      error = failure.what();
      return false;
    }
  }
  return true;
}
nlohmann::json Terrain3DComponent::schemaConstraints() {
  return {{"not",
           {{"required", {"asset", "recipe"}},
            {"properties", {{"asset", {{"minLength", 1}}}}}}}};
}
void Terrain3DComponent::parse(const nlohmann::json &json, Entity &entity) {
  std::string error;
  if (!validateAuthored(json, error))
    throw std::invalid_argument(error);
  Terrain3DComponent value;
  value.asset = json.value("asset", std::string{});
  if (json.contains("recipe"))
    value.recipe = json.at("recipe");
  entity.setComponent(std::move(value));
}
nlohmann::json Terrain3DComponent::defaults() {
  return nlohmann::json::object();
}
bool Terrain3DComponent::serializeField(const Terrain3DComponent &component,
                                        std::string_view field,
                                        nlohmann::json &out) {
  if (field == "asset")
    return readAsset(component, out);
  if (field == "recipe")
    return readRecipe(component, out);
  return false;
}
void Terrain3DComponent::copyRecipe(Terrain3DComponent &destination,
                                    const Terrain3DComponent &source) {
  destination.recipe = source.recipe;
  destination.generated.reset();
}
void Terrain3DComponent::copyAsset(Terrain3DComponent &destination,
                                   const Terrain3DComponent &source) {
  destination.asset = source.asset;
  destination.recipe = nullptr;
  destination.generated.reset();
}
bool Terrain3DComponent::readAsset(const Terrain3DComponent &source,
                                   nlohmann::json &out) {
  out = source.asset;
  return true;
}
bool Terrain3DComponent::readRecipe(const Terrain3DComponent &source,
                                    nlohmann::json &out) {
  if (!source.asset.empty() || source.recipe.is_null())
    return false;
  out = source.recipe;
  return true;
}
} // namespace demi::runtime
