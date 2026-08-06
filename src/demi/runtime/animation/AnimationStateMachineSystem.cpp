#include "demi/runtime/animation/AnimationStateMachineSystem.h"

#include "demi/runtime/animation/AnimationRuntime.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteAnimator2DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/components/animation/AnimationStateMachineComponent.h"

#include <algorithm>
#include <cmath>

namespace demi::runtime {
namespace {

bool transitionMatches(const AnimationTransition &transition,
                       const AnimationStateMachineComponent &machine,
                       const AnimationState &state) {
  if (!transition.from.empty() && transition.from != "*" &&
      transition.from != machine.state)
    return false;
  const float value = machine.parameters.contains(transition.parameter)
                          ? machine.parameters.at(transition.parameter)
                          : 0.0F;
  if (transition.condition == "trigger")
    return machine.triggers.contains(transition.parameter);
  if (transition.condition == "true")
    return value != 0.0F;
  if (transition.condition == "false")
    return value == 0.0F;
  if (transition.condition == "greater")
    return value > transition.threshold;
  if (transition.condition == "less")
    return value < transition.threshold;
  if (transition.condition == "finished")
    return state.duration > 0.0F && machine.time >= state.duration;
  return transition.condition == "always";
}

void applyState(Entity &entity, AnimationStateMachineComponent &machine,
                const AnimationState &state) {
  if (auto *sprite = entity.component<SpriteAnimator2DComponent>();
      sprite != nullptr && !state.spriteClip.empty() &&
      sprite->clips.contains(state.spriteClip)) {
    if (machine.entered || sprite->clip != state.spriteClip) {
      sprite->previousClip = sprite->clip;
      sprite->previousFrame = sprite->currentFrame;
      sprite->clip = state.spriteClip;
      sprite->time = 0.0F;
      sprite->currentFrame = sprite->clips.at(state.spriteClip).startFrame;
    }
    sprite->speed = state.speed;
    sprite->playing =
        state.loop || state.duration <= 0.0F || machine.time < state.duration;
  }
  if (auto *model = entity.component<AnimationPlayer3DComponent>();
      model != nullptr &&
      (state.modelClip >= 0 || !state.modelClipName.empty())) {
    if (machine.entered || model->clip != state.modelClip ||
        model->clipName != state.modelClipName) {
      model->previousClip = model->clip;
      model->previousClipName = model->clipName;
      model->previousTime = model->time;
      model->time = 0.0F;
    }
    if (state.modelClip >= 0)
      model->clip = state.modelClip;
    model->clipName = state.modelClipName;
    model->speed = state.speed;
    model->loop = state.loop;
    model->playing =
        state.loop || state.duration <= 0.0F || machine.time < state.duration;
  }
}

void emitEvents(World &world, const Entity &entity,
                const AnimationStateMachineComponent &machine,
                const AnimationState &state, const float previousTime,
                const float currentTime) {
  if (state.events.empty())
    return;
  const float duration = state.duration;
  if (!state.loop || duration <= 0.0F) {
    for (const AnimationStateEvent &event : state.events)
      if ((machine.entered && event.time == 0.0F) ||
          (event.time > previousTime && event.time <= currentTime))
        world.stateAnimationEvents.push_back({.entityId = entity.id,
                                              .clip = machine.state,
                                              .name = event.name,
                                              .frame = -1});
    return;
  }
  const int firstLoop =
      static_cast<int>(std::floor(previousTime / duration));
  const int lastLoop = static_cast<int>(std::floor(currentTime / duration));
  for (int loop = firstLoop; loop <= lastLoop; ++loop) {
    const float offset = static_cast<float>(loop) * duration;
    for (const AnimationStateEvent &event : state.events) {
      const float eventTime = offset + event.time;
      if ((machine.entered && eventTime == 0.0F) ||
          (eventTime > previousTime && eventTime <= currentTime))
        world.stateAnimationEvents.push_back({.entityId = entity.id,
                                              .clip = machine.state,
                                              .name = event.name,
                                              .frame = -1});
    }
  }
}

const AnimationState *sampledState(AnimationStateMachineComponent &machine,
                                   const AnimationState &fallback) {
  machine.blendSamples.clear();
  const auto found = machine.blendSpaces.find(machine.state);
  if (found == machine.blendSpaces.end())
    return &fallback;
  const AnimationBlendSpace &space = found->second;
  const float x = machine.parameters.contains(space.parameterX)
                      ? machine.parameters.at(space.parameterX)
                      : 0.0F;
  const float y = machine.parameters.contains(space.parameterY)
                      ? machine.parameters.at(space.parameterY)
                      : 0.0F;
  const auto samples =
      space.parameterY.empty() ? sampleBlendSpace1D(space, x)
                               : sampleBlendSpace2D(space, x, y);
  const AnimationState *result = &fallback;
  float greatest = -1.0F;
  for (const AnimationWeightedSample &sample : samples) {
    machine.blendSamples.emplace_back(sample.state, sample.weight);
    const auto state = machine.states.find(sample.state);
    if (state != machine.states.end() && sample.weight > greatest) {
      result = &state->second;
      greatest = sample.weight;
    }
  }
  return result;
}

} // namespace

void AnimationStateMachineSystem::update(World &world,
                                         const float deltaTime,
                                         const float unscaledDeltaTime) const {
  world.stateAnimationEvents.clear();
  for (Entity &entity : world.entities) {
    if (!entity.enabled)
      continue;
    auto *machine = entity.component<AnimationStateMachineComponent>();
    if (machine == nullptr) {
      if (auto *model = entity.component<AnimationPlayer3DComponent>();
          model != nullptr && model->playing)
        model->time += std::max(deltaTime, 0.0F) * model->speed;
      continue;
    }
    auto current = machine->states.find(machine->state);
    if (current == machine->states.end())
      continue;

    const float sourceDt =
        machine->updateWhenPaused && unscaledDeltaTime >= 0.0F
            ? unscaledDeltaTime
            : deltaTime;
    const float dt = std::max(sourceDt, 0.0F) * machine->speed;
    const float previousTime = machine->time;
    machine->time += dt;
    machine->normalizedTime =
        current->second.duration > 0.0F
            ? (current->second.loop
                   ? std::fmod(machine->time / current->second.duration, 1.0F)
                   : std::clamp(machine->time / current->second.duration,
                                0.0F, 1.0F))
            : 0.0F;
    emitEvents(world, entity, *machine, current->second, previousTime,
               machine->time);

    for (const AnimationTransition &transition : machine->transitions) {
      if (!machine->states.contains(transition.to) ||
          !transitionMatches(transition, *machine, current->second))
        continue;
      if (transition.condition == "trigger")
        machine->triggers.erase(transition.parameter);
      machine->activeTransition = {.from = machine->state,
                                   .to = transition.to,
                                   .duration = transition.blendDuration,
                                   .elapsed = 0.0F,
                                   .active =
                                       transition.blendDuration > 0.0F};
      machine->state = transition.to;
      machine->time = 0.0F;
      machine->normalizedTime = 0.0F;
      machine->entered = true;
      current = machine->states.find(machine->state);
      break;
    }
    if (machine->activeTransition.active) {
      machine->activeTransition.elapsed += dt;
      if (machine->activeTransition.elapsed >=
          machine->activeTransition.duration)
        machine->activeTransition.active = false;
    }
    const float blendWeight =
        machine->activeTransition.active
            ? std::clamp(machine->activeTransition.elapsed /
                             machine->activeTransition.duration,
                         0.0F, 1.0F)
            : 1.0F;
    if (auto *sprite = entity.component<SpriteAnimator2DComponent>())
      sprite->blendWeight = blendWeight;
    if (auto *model = entity.component<AnimationPlayer3DComponent>())
      model->blendWeight = blendWeight;

    const AnimationState *presentation =
        sampledState(*machine, current->second);
    applyState(entity, *machine, *presentation);
    if (auto *model = entity.component<AnimationPlayer3DComponent>()) {
      model->time = machine->time;
      model->layers.clear();
      for (const AnimationLayer &layer : machine->layers) {
        const auto state = machine->states.find(layer.state);
        if (state == machine->states.end() || layer.weight <= 0.0F)
          continue;
        model->layers.push_back(
            {.clip = state->second.modelClip,
             .clipName = state->second.modelClipName,
             .mask = layer.mask,
             .weight = layer.weight,
             .additive = layer.additive});
      }
    }
    if (machine->rootMotion && dt > 0.0F) {
      if (auto *transform = entity.component<Transform3DComponent>()) {
        const Vec3 motion =
            presentation->rootMotionTrack.size() >= 2
                ? extractRootMotion(presentation->rootMotionTrack, previousTime,
                                    machine->time, presentation->duration,
                                    presentation->loop)
                : Vec3{
                      .x = presentation->rootMotionPerSecond.x * dt,
                      .y = presentation->rootMotionPerSecond.y * dt,
                      .z = presentation->rootMotionPerSecond.z * dt};
        transform->position.x += motion.x;
        transform->position.y += motion.y;
        transform->position.z += motion.z;
      }
    }
    machine->entered = false;
  }
}

} // namespace demi::runtime
