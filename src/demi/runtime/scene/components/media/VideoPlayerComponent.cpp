#include "demi/runtime/scene/components/media/VideoPlayerComponent.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"
namespace demi::runtime {
void VideoPlayerComponent::parse(const nlohmann::json &json, Entity &entity) {
  VideoPlayerComponent component;
  component.clip = scene_loading::stringOr(json, "clip");
  component.playOnStart =
      scene_loading::boolField(json, "play_on_start").value_or(false);
  component.loop = scene_loading::boolField(json, "loop").value_or(false);
  entity.setComponent(std::move(component));
}
} // namespace demi::runtime
