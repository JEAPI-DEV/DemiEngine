#include "demi/runtime/scene/components/animation/AnimationStateMachineComponent.h"

#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>

namespace demi::runtime {

void AnimationStateMachineComponent::parse(const nlohmann::json &json,
                                           Entity &entity) {
  AnimationStateMachineComponent component;
  if (const auto *states = scene_loading::objectField(json, "states")) {
    for (const auto &[name, value] : states->items()) {
      if (!value.is_object())
        continue;
      AnimationState state;
      state.spriteClip = scene_loading::stringOr(value, "sprite_clip");
      state.modelClip = static_cast<int>(
          scene_loading::numberField(value, "model_clip").value_or(-1.0F));
      state.modelClipName = scene_loading::stringOr(value, "model_clip_name");
      state.duration = std::max(
          scene_loading::numberField(value, "duration").value_or(0.0F), 0.0F);
      state.speed = std::max(
          scene_loading::numberField(value, "speed").value_or(1.0F), 0.0F);
      state.loop = scene_loading::boolField(value, "loop").value_or(true);
      state.rootMotionTrack =
          scene_loading::vec3ArrayField(value, "root_motion_track");
      if (const auto rootMotion =
              scene_loading::vec3Field(value, "root_motion_per_second"))
        state.rootMotionPerSecond = *rootMotion;
      if (const auto *events = scene_loading::arrayField(value, "events")) {
        for (const auto &event : *events) {
          const auto eventName = scene_loading::stringOr(event, "name");
          if (!eventName.empty())
            state.events.push_back(
                {.time = std::max(
                     scene_loading::numberField(event, "time").value_or(0.0F),
                     0.0F),
                 .name = eventName});
        }
        std::stable_sort(state.events.begin(), state.events.end(),
                         [](const auto &left, const auto &right) {
                           return left.time < right.time;
                         });
      }
      component.states.emplace(name, std::move(state));
    }
  }
  if (const auto *transitions =
          scene_loading::objectField(json, "transitions")) {
    for (const auto &[unused, value] : transitions->items()) {
      (void)unused;
      if (!value.is_object())
        continue;
      AnimationTransition transition;
      transition.from = scene_loading::stringOr(value, "from");
      transition.to = scene_loading::stringOr(value, "to");
      transition.parameter = scene_loading::stringOr(value, "parameter");
      transition.condition = scene_loading::stringOr(value, "condition");
      if (transition.condition.empty())
        transition.condition = "always";
      transition.threshold =
          scene_loading::numberField(value, "threshold").value_or(0.0F);
      transition.blendDuration = std::max(
          scene_loading::numberField(value, "blend_duration").value_or(0.0F),
          0.0F);
      if (!transition.to.empty())
        component.transitions.push_back(std::move(transition));
    }
  }
  if (const auto *spaces =
          scene_loading::objectField(json, "blend_spaces")) {
    for (const auto &[name, value] : spaces->items()) {
      if (!value.is_object())
        continue;
      AnimationBlendSpace space;
      space.parameterX = scene_loading::stringOr(value, "parameter_x");
      space.parameterY = scene_loading::stringOr(value, "parameter_y");
      if (const auto *points = scene_loading::arrayField(value, "points")) {
        for (const auto &point : *points) {
          const std::string stateName =
              scene_loading::stringOr(point, "state");
          if (!stateName.empty())
            space.points.push_back(
                {.state = stateName,
                 .x = scene_loading::numberField(point, "x").value_or(0.0F),
                 .y = scene_loading::numberField(point, "y").value_or(0.0F)});
        }
      }
      component.blendSpaces[name] = std::move(space);
    }
  }
  if (const auto *layers = scene_loading::objectField(json, "layers")) {
    for (const auto &[name, value] : layers->items()) {
      if (!value.is_object())
        continue;
      AnimationLayer layer;
      layer.name = name;
      layer.state = scene_loading::stringOr(value, "state");
      layer.weight = std::clamp(
          scene_loading::numberField(value, "weight").value_or(1.0F), 0.0F,
          1.0F);
      layer.additive =
          scene_loading::boolField(value, "additive").value_or(false);
      if (const auto *mask = scene_loading::arrayField(value, "mask"))
        for (const auto &bone : *mask)
          if (bone.is_string())
            layer.mask.push_back(bone.get<std::string>());
      component.layers.push_back(std::move(layer));
    }
    std::ranges::sort(component.layers, {}, &AnimationLayer::name);
  }
  if (const auto *parameters = scene_loading::objectField(json, "parameters")) {
    for (const auto &[name, value] : parameters->items()) {
      if (value.is_boolean())
        component.parameters[name] = value.get<bool>() ? 1.0F : 0.0F;
      else if (value.is_number())
        component.parameters[name] = value.get<float>();
    }
  }
  component.state = scene_loading::stringOr(json, "initial_state");
  component.speed = std::max(
      scene_loading::numberField(json, "speed").value_or(1.0F), 0.0F);
  component.rootMotion =
      scene_loading::boolField(json, "root_motion").value_or(false);
  component.updateWhenPaused =
      scene_loading::stringOr(json, "pause_policy", "pause") == "continue";
  if (!component.states.contains(component.state) &&
      !component.states.empty()) {
    component.state =
        std::min_element(component.states.begin(), component.states.end(),
                         [](const auto &left, const auto &right) {
                           return left.first < right.first;
                         })
            ->first;
  }
  entity.setComponent(std::move(component));
}

} // namespace demi::runtime
