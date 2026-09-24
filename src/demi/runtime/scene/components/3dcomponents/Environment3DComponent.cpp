#include "demi/runtime/scene/components/3dcomponents/Environment3DComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace demi::runtime {
nlohmann::json Environment3DComponent::defaults() {
  const Environment3DComponent c;
  return {{"sky_texture",c.skyTexture},{"ambient_color",{c.ambientColor.r,c.ambientColor.g,c.ambientColor.b,c.ambientColor.a}},
    {"ambient_intensity",c.ambientIntensity},{"fog_color",{c.fogColor.r,c.fogColor.g,c.fogColor.b,c.fogColor.a}},
    {"fog_start",c.fogStart},{"fog_end",c.fogEnd},{"shadow_distance",c.shadowDistance},
    {"shadow_resolution",c.shadowResolution},{"max_shadow_lights",c.maxShadowLights},
    {"relief_cache_meshes",c.reliefCacheMeshes},{"relief_image_cache_mb",c.reliefImageCacheBytes/(1024U*1024U)}};
}
void Environment3DComponent::parse(const nlohmann::json &json,
                                   Entity &entity) {
  Environment3DComponent component;
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
  component.shadowResolution = std::clamp(
      static_cast<int>(
          scene_loading::numberField(json, "shadow_resolution")
              .value_or(1024.0F)),
      128, 4096);
  component.maxShadowLights = std::clamp(
      static_cast<int>(
          scene_loading::numberField(json, "max_shadow_lights").value_or(1.0F)),
      0, 4);
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
