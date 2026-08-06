#include "demi/runtime/scene/components/3dcomponents/ParticleEmitter3DComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>

namespace demi::runtime {
void ParticleEmitter3DComponent::parse(const nlohmann::json &json,
                                       Entity &entity) {
  ParticleEmitter3DComponent component;
  component.emissionShape =
      scene_loading::stringOr(json, "emission_shape", "point");
  if (auto value = scene_loading::vec3Field(json, "emission_size"))
    component.emissionSize = *value;
  component.rate =
      std::max(scene_loading::numberField(json, "rate").value_or(10.0F), 0.0F);
  component.burst = std::max(
      static_cast<int>(scene_loading::numberField(json, "burst").value_or(0)),
      0);
  component.lifetime = std::max(
      scene_loading::numberField(json, "lifetime").value_or(1.0F), 0.001F);
  if (auto value = scene_loading::vec3Field(json, "velocity_min"))
    component.velocityMin = *value;
  if (auto value = scene_loading::vec3Field(json, "velocity_max"))
    component.velocityMax = *value;
  if (auto value = scene_loading::vec3Field(json, "gravity"))
    component.gravity = *value;
  component.sizeStart = std::max(
      scene_loading::numberField(json, "size_start").value_or(0.15F), 0.0F);
  component.sizeEnd = std::max(
      scene_loading::numberField(json, "size_end").value_or(0.0F), 0.0F);
  component.rotationSpeed =
      scene_loading::numberField(json, "rotation_speed").value_or(0.0F);
  if (auto value = scene_loading::colorField(json, "color_start"))
    component.colorStart = *value;
  if (auto value = scene_loading::colorField(json, "color_end"))
    component.colorEnd = *value;
  component.texture = scene_loading::stringOr(json, "texture");
  component.material = scene_loading::stringOr(json, "material");
  component.simulationSpace =
      scene_loading::stringOr(json, "simulation_space", "world");
  component.sortingOrder = static_cast<int>(
      scene_loading::numberField(json, "sorting_order").value_or(0.0F));
  component.renderMask = scene_loading::stringOr(json, "render_mask");
  component.seed = static_cast<std::uint32_t>(std::max(
      scene_loading::numberField(json, "seed").value_or(1.0F), 0.0F));
  component.maxParticles = std::max(
      static_cast<int>(
          scene_loading::numberField(json, "max_particles").value_or(256.0F)),
      1);
  component.mobileMaxParticles = std::max(
      static_cast<int>(scene_loading::numberField(json, "mobile_max_particles")
                           .value_or(96.0F)),
      1);
  component.playing =
      scene_loading::boolField(json, "playing").value_or(true);
  component.loop = scene_loading::boolField(json, "loop").value_or(true);
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
