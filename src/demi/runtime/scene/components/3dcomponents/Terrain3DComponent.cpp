#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace demi::runtime {
bool Terrain3DComponent::validateAuthored(const nlohmann::json &json,
                                          std::string &error) {
  if (!json.is_object() ||
      (json.contains("asset") && !json.at("asset").is_string())) {
    error = "Terrain3D requires an object and a string asset reference";
    return false;
  }
  if (const auto blending = json.find("surface_blending");
      blending != json.end() && !blending->is_boolean()) {
    error = "Terrain3D surface_blending requires a boolean";
    return false;
  }
  if (const auto quality = json.find("lod_error_pixels");
      quality != json.end()) {
    if (!quality->is_number()) {
      error = "Terrain3D lod_error_pixels requires a number";
      return false;
    }
    const double value = quality->get<double>();
    if (!std::isfinite(value) || value < 0.0 ||
        value > std::numeric_limits<float>::max()) {
      error = "Terrain3D lod_error_pixels must be finite and nonnegative";
      return false;
    }
  }
  if (const auto exclusions = json.find("scatter_exclusions");
      exclusions != json.end()) {
    if (!exclusions->is_array()) {
      error = "Terrain3D scatter_exclusions requires an array of stable "
              "placement keys";
      return false;
    }
    std::set<std::string> unique;
    for (const auto &key : *exclusions) {
      if (!key.is_string() || key.get_ref<const std::string &>().empty()) {
        error = "Terrain3D scatter_exclusions requires nonempty string "
                "placement keys";
        return false;
      }
      if (!unique.insert(key.get<std::string>()).second) {
        error = "Terrain3D scatter_exclusions must not repeat a placement key";
        return false;
      }
    }
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
  nlohmann::json constraints{
      {"not",
       {{"required", {"asset", "recipe"}},
        {"properties", {{"asset", {{"minLength", 1}}}}}}}};
  const nlohmann::json exclusionItems{{"uniqueItems", true},
                                      {"items", {{"minLength", 1}}}};
  constraints["allOf"] = nlohmann::json::array(
      {{{"properties", {{"scatter_exclusions", exclusionItems}}}}});
  return constraints;
}

void Terrain3DComponent::parse(const nlohmann::json &json, Entity &entity) {
  std::string error;
  if (!validateAuthored(json, error)) {
    throw std::invalid_argument(error);
  }
  Terrain3DComponent value;
  value.asset = json.value("asset", std::string{});
  value.lodErrorPixels = json.value("lod_error_pixels", value.lodErrorPixels);
  value.surfaceBlending = json.value("surface_blending", value.surfaceBlending);
  value.scatterExclusions =
      json.value("scatter_exclusions", value.scatterExclusions);
  if (json.contains("recipe")) {
    value.recipe = json.at("recipe");
  }
  entity.setComponent(std::move(value));
}
nlohmann::json Terrain3DComponent::defaults() {
  return nlohmann::json::object();
}
bool Terrain3DComponent::serializeField(const Terrain3DComponent &component,
                                        std::string_view field,
                                        nlohmann::json &out) {
  if (field == "asset") {
    return readAsset(component, out);
  }
  if (field == "recipe") {
    return readRecipe(component, out);
  }
  if (field == "scatter_exclusions") {
    return runtimeFieldJson(component.scatterExclusions, out);
  }
  if (field == "surface_blending") {
    return runtimeFieldJson(component.surfaceBlending, out);
  }
  if (field == "lod_error_pixels") {
    return runtimeFieldJson(component.lodErrorPixels, out);
  }
  return false;
}
void Terrain3DComponent::copyRecipe(Terrain3DComponent &destination,
                                    const Terrain3DComponent &source) {
  destination.recipe = source.recipe;
  destination.generated.reset();
  destination.region.reset();
}
void Terrain3DComponent::copyAsset(Terrain3DComponent &destination,
                                   const Terrain3DComponent &source) {
  destination.asset = source.asset;
  destination.recipe = nullptr;
  destination.generated.reset();
  destination.region.reset();
}
bool Terrain3DComponent::readAsset(const Terrain3DComponent &source,
                                   nlohmann::json &out) {
  out = source.asset;
  return true;
}
bool Terrain3DComponent::readRecipe(const Terrain3DComponent &source,
                                    nlohmann::json &out) {
  if (!source.asset.empty() || source.recipe.is_null()) {
    return false;
  }
  out = source.recipe;
  return true;
}
} // namespace demi::runtime
