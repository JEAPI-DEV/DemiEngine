#include "demi/runtime/scene/components/2dcomponents/SpriteAnimator2DComponent.h"

#include "demi/runtime/animation/SpriteSheetAnimation.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime {

void SpriteAnimator2DComponent::copyClips(
    SpriteAnimator2DComponent &destination,
    const SpriteAnimator2DComponent &source) {
  destination.clips = source.clips;
  // Keep gameplay's active clip when it survives the edit. Otherwise use the
  // parser's authored/automatic selection, including its empty-map behavior.
  if (!destination.clips.contains(destination.clip))
    destination.clip = source.clip;
}

void SpriteAnimator2DComponent::afterRuntimeFieldChange(
    SpriteAnimator2DComponent &component, const std::string_view field) {
  if (field != "clip" && field != "atlas" && field != "clips")
    return;
  const auto selected = component.clips.find(component.clip);
  if (selected == component.clips.end()) {
    component.currentFrame = 0;
    return;
  }
  const auto &clip = selected->second;
  // Refresh the sampled frame even while paused. Field edits do not advance
  // time, emit events, restart playback, or replace transition history.
  const double tick = std::floor(component.time * clip.framesPerSecond);
  const double localFrame =
      clip.loop
          ? std::max(std::fmod(tick, clip.frameCount), 0.0)
          : std::clamp(tick, 0.0, static_cast<double>(clip.frameCount - 1));
  component.currentFrame = clip.startFrame + static_cast<int>(localFrame);
}

void SpriteAnimator2DComponent::parse(const nlohmann::json &json,
                                      Entity &entity) {
  SpriteAnimator2DComponent component;
  if (const auto value = scene_loading::vec2Field(json, "frame_size"))
    component.frameSize = *value;
  component.clip = scene_loading::stringOr(json, "clip");
  component.speed =
      std::max(scene_loading::numberField(json, "speed").value_or(1.0F), 0.0F);
  component.playing = scene_loading::boolField(json, "playing").value_or(true);

  if (const auto *atlas = scene_loading::objectField(json, "atlas")) {
    std::vector<std::string> rowNames;
    if (const auto *names = scene_loading::arrayField(*atlas, "row_names"))
      for (const auto &name : *names)
        if (name.is_string())
          rowNames.push_back(name.get<std::string>());
    component.clips = makeSpriteSheetRows(
        static_cast<int>(
            scene_loading::numberField(*atlas, "columns").value_or(1.0F)),
        static_cast<int>(
            scene_loading::numberField(*atlas, "rows").value_or(1.0F)),
        rowNames,
        scene_loading::numberField(*atlas, "fps").value_or(10.0F),
        scene_loading::boolField(*atlas, "loop").value_or(true));
  }

  if (const auto *clips = scene_loading::objectField(json, "clips")) {
    for (const auto &[name, value] : clips->items()) {
      if (!value.is_object())
        continue;
      SpriteAnimationClip2D clip;
      clip.startFrame = std::max(
          static_cast<int>(
              scene_loading::numberField(value, "start_frame").value_or(0.0F)),
          0);
      clip.frameCount = std::max(
          static_cast<int>(
              scene_loading::numberField(value, "frame_count").value_or(1.0F)),
          1);
      clip.framesPerSecond = std::max(
          scene_loading::numberField(value, "fps").value_or(10.0F), 0.01F);
      clip.loop = scene_loading::boolField(value, "loop").value_or(true);
      if (const auto *events = scene_loading::arrayField(value, "events")) {
        for (const auto &event : *events) {
          const std::string eventName = scene_loading::stringOr(event, "name");
          if (!eventName.empty())
            clip.events.push_back(
                {.frame = std::clamp(
                     static_cast<int>(scene_loading::numberField(event, "frame")
                                          .value_or(0.0F)),
                     0, clip.frameCount - 1),
                 .name = eventName});
        }
      }
      component.clips[name] = std::move(clip);
    }
  }
  if (component.clip.empty() && !component.clips.empty()) {
    const auto first =
        std::min_element(component.clips.begin(), component.clips.end(),
                         [](const auto &left, const auto &right) {
                           return left.first < right.first;
                         });
    component.clip = first->first;
  }
  if (const auto selected = component.clips.find(component.clip);
      selected != component.clips.end())
    component.currentFrame = selected->second.startFrame;
  entity.setComponent(std::move(component));
}

} // namespace demi::runtime
