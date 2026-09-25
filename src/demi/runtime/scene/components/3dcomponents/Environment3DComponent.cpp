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
  return {{"msaa_samples",c.msaaSamples},{"sky_texture",c.skyTexture},{"ambient_color",{c.ambientColor.r,c.ambientColor.g,c.ambientColor.b,c.ambientColor.a}},
    {"ambient_intensity",c.ambientIntensity},{"fog_color",{c.fogColor.r,c.fogColor.g,c.fogColor.b,c.fogColor.a}},
    {"fog_start",c.fogStart},{"fog_end",c.fogEnd},{"shadow_distance",c.shadowDistance},
    {"shadow_resolution",c.shadowResolution},{"shadow_bias",c.shadowBias},{"max_shadow_lights",c.maxShadowLights},
    {"relief_cache_meshes",c.reliefCacheMeshes},{"relief_image_cache_mb",c.reliefImageCacheBytes/(1024U*1024U)}};
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
  component.shadowDistance = std::max(
      scene_loading::numberField(json, "shadow_distance").value_or(80.0F),
      0.0F);
  const double resolution=json.value("shadow_resolution",1024.0);
  const double budget=json.value("max_shadow_lights",1.0);
  component.shadowBias=json.value("shadow_bias",.02F);
  if(!std::isfinite(resolution) || resolution<1 || resolution>65535 || std::floor(resolution)!=resolution ||
     !std::isfinite(component.shadowDistance*8.F) || !std::isfinite(component.shadowBias) || component.shadowBias<0 ||
     !std::isfinite(budget) || budget<0 || budget>INT32_MAX || std::floor(budget)!=budget)
    throw std::invalid_argument("Invalid shadow resolution, distance, bias or light budget");
  component.shadowResolution=int(resolution);
  component.maxShadowLights=int(budget);
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
