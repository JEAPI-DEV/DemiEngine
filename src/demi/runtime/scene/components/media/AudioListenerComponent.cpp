#include "demi/runtime/scene/components/media/AudioListenerComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void AudioListenerComponent::parse(const nlohmann::json &json, Entity &entity) {
  AudioListenerComponent component;
  component.primary = scene_loading::boolField(json, "primary").value_or(true);
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
