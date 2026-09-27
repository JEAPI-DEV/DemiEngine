#include "demi/runtime/scene/components/3dcomponents/Environment3DComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <cmath>

namespace demi::runtime {
bool Environment3DComponent::serializeField(
    const Environment3DComponent &component, std::string_view field,
    nlohmann::json &out) {
  if (field != "relief_image_cache_mb")
    return false;
  out = component.reliefImageCacheBytes / (1024U * 1024U);
  return true;
}

nlohmann::json Environment3DComponent::defaults() {
  const Environment3DComponent c;
  return {
      {"msaa_samples", c.msaaSamples},
      {"sky_texture", c.skyTexture},
      {"ambient_color",
       {c.ambientColor.r, c.ambientColor.g, c.ambientColor.b,
        c.ambientColor.a}},
      {"ambient_intensity", c.ambientIntensity},
      {"fog_color", {c.fogColor.r, c.fogColor.g, c.fogColor.b, c.fogColor.a}},
      {"fog_start", c.fogStart},
      {"fog_end", c.fogEnd},
      {"shadow_distance", c.shadowDistance},
      {"shadow_resolution", c.shadowResolution},
      {"shadow_cascades", c.shadowCascades},
      {"shadow_split_lambda", c.shadowSplitLambda},
      {"shadow_blend", c.shadowBlend},
      {"shadow_filter", c.shadowFilter},
      {"shadow_bias", c.shadowBias},
      {"max_shadow_lights", c.maxShadowLights},
      {"relief_cache_meshes", c.reliefCacheMeshes},
      {"relief_image_cache_mb", c.reliefImageCacheBytes / (1024U * 1024U)}};
}
void Environment3DComponent::parse(const nlohmann::json &json,
                                   Entity &entity) {
  Environment3DComponent component;
  const double samples=json.value("msaa_samples",4.0);
  if(std::ranges::none_of(msaaOptions,[&](int option){return samples==option;}))
    throw std::invalid_argument("Environment3D.msaa_samples must be 0 (off), 2, 4, 8 or 16");
  component.msaaSamples=int(samples);
  const auto meshes=json.value("relief_cache_meshes",std::int64_t(256));
  const auto megabytes=json.value("relief_image_cache_mb",std::int64_t(64));
  if(meshes<0 || megabytes<0 || std::uint64_t(megabytes)>std::numeric_limits<std::size_t>::max()/(1024U*1024U))
    throw std::invalid_argument("Relief retention budgets must be nonnegative and fit addressable memory");
  component.reliefCacheMeshes=static_cast<std::size_t>(meshes);
  component.reliefImageCacheBytes=static_cast<std::size_t>(megabytes)*1024U*1024U;
  component.skyTexture = json.value("sky_texture", std::string{});
  if (auto value = scene_loading::colorField(json, "ambient_color"))
    component.ambientColor = *value;
  component.ambientIntensity = std::max(
      scene_loading::numberField(json, "ambient_intensity").value_or(0.5F),
      0.0F);
  if (auto value = scene_loading::colorField(json, "fog_color"))
    component.fogColor = *value;
  component.fogStart = std::max(
      scene_loading::numberField(json, "fog_start").value_or(80.0F), 0.0F);
  component.fogEnd =
      std::max(scene_loading::numberField(json, "fog_end").value_or(220.0F),
               component.fogStart + 0.001F);
  const double distance = json.value("shadow_distance", 80.0);
  const double resolution = json.value("shadow_resolution", 1024.0);
  const double cascades = json.value("shadow_cascades", 4.0);
  const double splitLambda = json.value("shadow_split_lambda", 0.85);
  const double blend = json.value("shadow_blend", 0.1);
  component.shadowFilter = json.value("shadow_filter", std::string{"pcf"});
  const double bias = json.value("shadow_bias", 0.005);
  const double budget = json.value("max_shadow_lights", 1.0);
  if (!std::isfinite(distance) || distance < 0 ||
      distance > std::numeric_limits<float>::max())
    throw std::invalid_argument("Environment3D.shadow_distance must be a "
                                "nonnegative finite distance in metres");
  if (!std::isfinite(resolution) || resolution < 1 || resolution > 65535 ||
      std::floor(resolution) != resolution)
    throw std::invalid_argument(
        "Environment3D.shadow_resolution must be an integer from 1 to 65535");
  if (std::ranges::none_of(shadowCascadeOptions, [cascades](int option) {
        return cascades == option;
      }))
    throw std::invalid_argument(
        "Environment3D.shadow_cascades must be an integer from 1 to 4");
  if (!std::isfinite(splitLambda) || splitLambda < 0 || splitLambda > 1)
    throw std::invalid_argument(
        "Environment3D.shadow_split_lambda must be between 0 and 1");
  if (!std::isfinite(blend) || blend < 0 || blend > 1)
    throw std::invalid_argument(
        "Environment3D.shadow_blend must be between 0 and 1");
  if (std::ranges::find(shadowFilterOptions, component.shadowFilter) ==
      shadowFilterOptions.end())
    throw std::invalid_argument(
        "Environment3D.shadow_filter must be hard or pcf");
  if (!std::isfinite(bias) || bias < 0 ||
      bias > std::numeric_limits<float>::max())
    throw std::invalid_argument("Environment3D.shadow_bias must be a "
                                "nonnegative finite distance in metres");
  if (!std::isfinite(budget) || budget < 0 || budget > INT32_MAX ||
      std::floor(budget) != budget)
    throw std::invalid_argument(
        "Environment3D.max_shadow_lights must be a nonnegative 32-bit integer");
  component.shadowDistance = static_cast<float>(distance);
  component.shadowResolution = static_cast<int>(resolution);
  component.shadowCascades = static_cast<int>(cascades);
  component.shadowSplitLambda = static_cast<float>(splitLambda);
  component.shadowBlend = static_cast<float>(blend);
  component.shadowBias = static_cast<float>(bias);
  component.maxShadowLights = static_cast<int>(budget);
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
