#include "demi/runtime/animation/AnimationStateMachineSystem.h"

#include "demi/runtime/scene/components/2dcomponents/SpriteAnimator2DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/animation/AnimationStateMachineComponent.h"

#include <algorithm>

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
      sprite->clip = state.spriteClip;
      sprite->time = 0.0F;
      sprite->currentFrame = sprite->clips.at(state.spriteClip).startFrame;
    }
    sprite->speed = state.speed;
    sprite->playing =
        state.loop || state.duration <= 0.0F || machine.time < state.duration;
  }
  if (auto *model = entity.component<AnimationPlayer3DComponent>();
      model != nullptr && state.modelClip >= 0) {
    if (machine.entered || model->clip != state.modelClip)
      model->time = 0.0F;
    model->clip = state.modelClip;
    model->speed = state.speed;
    model->loop = state.loop;
    model->playing =
        state.loop || state.duration <= 0.0F || machine.time < state.duration;
  }
}

} // namespace

void AnimationStateMachineSystem::update(World &world,
                                         const float deltaTime) const {
  world.stateAnimationEvents.clear();
  const float dt = std::max(deltaTime, 0.0F);
  for (Entity &entity : world.entities) {
    auto *machine = entity.component<AnimationStateMachineComponent>();
    if (machine == nullptr)
      continue;
    auto current = machine->states.find(machine->state);
    if (current == machine->states.end())
      continue;

    const float previousTime = machine->time;
    machine->time += dt;
    for (const AnimationStateEvent &event : current->second.events) {
      if ((machine->entered && event.time == 0.0F) ||
          (event.time > previousTime && event.time <= machine->time))
        world.stateAnimationEvents.push_back({.entityId = entity.id,
                                              .clip = machine->state,
                                              .name = event.name,
                                              .frame = -1});
    }

    for (const AnimationTransition &transition : machine->transitions) {
      if (!machine->states.contains(transition.to) ||
          !transitionMatches(transition, *machine, current->second))
        continue;
      if (transition.condition == "trigger")
        machine->triggers.erase(transition.parameter);
      machine->state = transition.to;
      machine->time = 0.0F;
      machine->entered = true;
      current = machine->states.find(machine->state);
      break;
    }
    applyState(entity, *machine, current->second);
    machine->entered = false;
  }
}

} // namespace demi::runtime
