#include "demi/runtime/animation/AnimationRuntime.h"
#include "demi/runtime/animation/AnimationStateMachineSystem.h"
#include "demi/runtime/animation/SpriteSheetAnimation.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteAnimator2DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/components/animation/AnimationStateMachineComponent.h"

#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace demi::runtime;

namespace {
bool near(float left, float right) {
  return std::abs(left - right) < 0.001F;
}
} // namespace

int main() {
  AnimationBlendSpace line{.parameterX = "speed",
                           .points = {{"idle", 0.0F, 0.0F},
                                      {"walk", 1.0F, 0.0F},
                                      {"run", 3.0F, 0.0F}}};
  const auto middle = sampleBlendSpace1D(line, 2.0F);
  if (middle.size() != 2 || middle[0].state != "walk" ||
      !near(middle[0].weight, 0.5F) ||
      !near(middle[1].weight, 0.5F) ||
      sampleBlendSpace1D(line, -10.0F).front().state != "idle") {
    std::cerr << "1D blend-space interpolation failed.\n";
    return 1;
  }
  AnimationBlendSpace plane{.parameterX = "x",
                            .parameterY = "y",
                            .points = {{"center", 0.0F, 0.0F},
                                       {"right", 1.0F, 0.0F},
                                       {"up", 0.0F, 1.0F},
                                       {"left", -1.0F, 0.0F}}};
  if (sampleBlendSpace2D(plane, 0.0F, 0.0F).front().state != "center") {
    std::cerr << "2D blend-space exact-point handling failed.\n";
    return 1;
  }
  const auto diagonal = sampleBlendSpace2D(plane, 0.5F, 0.5F);
  float total = 0.0F;
  for (const auto &sample : diagonal)
    total += sample.weight;
  if (diagonal.size() != 3 || !near(total, 1.0F)) {
    std::cerr << "2D blend-space weights were not normalized.\n";
    return 1;
  }
  const std::vector<Vec3> rootTrack{{0.0F, 0.0F, 0.0F},
                                    {1.0F, 0.0F, 0.0F},
                                    {2.0F, 0.0F, 0.0F}};
  const Vec3 withinCycle = extractRootMotion(rootTrack, 0.25F, 0.75F, 1.0F,
                                             true);
  const Vec3 acrossCycle = extractRootMotion(rootTrack, 0.75F, 1.25F, 1.0F,
                                             true);
  const Vec3 manyCycles = extractRootMotion(rootTrack, 0.25F, 3.25F, 1.0F,
                                            true);
  const Vec3 clamped = extractRootMotion(rootTrack, 0.75F, 2.0F, 1.0F,
                                        false);
  if (!near(withinCycle.x, 1.0F) || !near(acrossCycle.x, 1.0F) ||
      !near(manyCycles.x, 6.0F) || !near(clamped.x, 0.5F) ||
      !near(extractRootMotion({}, 0.0F, 1.0F, 1.0F, true).x, 0.0F)) {
    std::cerr << "Root-motion track extraction failed at a boundary.\n";
    return 1;
  }

  const auto rows = makeSpriteSheetRows(4, 3, {"idle", "", "attack"}, 12.0F);
  if (rows.size() != 2 || rows.at("attack").startFrame != 8 ||
      rows.at("attack").frameCount != 4) {
    std::cerr << "Sprite-sheet row import helper failed.\n";
    return 1;
  }

  Entity entity;
  entity.id = "actor";
  Transform3DComponent::parse(
      nlohmann::json::parse(R"({"position":[0,0,0]})"), entity);
  AnimationPlayer3DComponent::parse(
      nlohmann::json::parse(R"({"clip_name":"Idle"})"), entity);
  AnimationStateMachineComponent::parse(nlohmann::json::parse(R"({
    "initial_state":"idle",
    "root_motion":true,
    "pause_policy":"continue",
    "states":{
      "idle":{"model_clip_name":"Idle","duration":1.0,"loop":true,
              "events":[{"time":0.25,"name":"step"}]},
      "attack":{"model_clip_name":"Attack","duration":0.5,"loop":false,
                "root_motion_track":[[0,0,0],[1,0,0]]}
    },
    "transitions":{
      "attack":{"from":"idle","to":"attack","condition":"trigger",
                "parameter":"attack","blend_duration":0.2},
      "done":{"from":"attack","to":"idle","condition":"finished",
              "blend_duration":0.1}
    },
    "layers":{
      "upper":{"state":"attack","weight":0.75,"additive":true,
               "mask":["spine","arm"]}
    }
  })"), entity);
  auto *machine = entity.component<AnimationStateMachineComponent>();
  if (!validateAnimationMachine(*machine).empty()) {
    std::cerr << "A valid animation machine failed validation.\n";
    return 1;
  }
  World world;
  world.entities.push_back(std::move(entity));
  AnimationStateMachineSystem system;
  system.update(world, 0.3F);
  machine =
      world.entities.front().component<AnimationStateMachineComponent>();
  if (world.stateAnimationEvents.size() != 1 ||
      world.stateAnimationEvents.front().name != "step" ||
      !near(machine->normalizedTime, 0.3F)) {
    std::cerr << "Animation events or normalized time failed.\n";
    return 1;
  }
  machine->triggers.insert("attack");
  system.update(world, 0.0F);
  machine =
      world.entities.front().component<AnimationStateMachineComponent>();
  auto *player =
      world.entities.front().component<AnimationPlayer3DComponent>();
  if (machine->state != "attack" || !machine->activeTransition.active ||
      player->previousClipName != "Idle" || player->clipName != "Attack" ||
      !near(player->blendWeight, 0.0F) || player->layers.size() != 1 ||
      !player->layers.front().additive ||
      player->layers.front().mask.size() != 2) {
    std::cerr << "Cross-fade state was not initialized.\n";
    return 1;
  }
  system.update(world, 0.1F);
  if (!near(player->blendWeight, 0.5F) ||
      !near(machine->normalizedTime, 0.2F) ||
      !near(world.entities.front()
                .component<Transform3DComponent>()
                ->position.x,
            0.2F)) {
    std::cerr << "Cross-fade, normalized time, or root motion failed.\n";
    return 1;
  }
  system.update(world, 0.0F, 0.1F);
  if (!near(world.entities.front()
                .component<Transform3DComponent>()
                ->position.x,
            0.4F)) {
    std::cerr << "Explicit continue-while-paused policy failed.\n";
    return 1;
  }
  machine->rootMotion = false;
  system.update(world, 0.1F);
  if (!near(world.entities.front()
                .component<Transform3DComponent>()
                ->position.x,
            0.4F)) {
    std::cerr << "Root motion ignored its gameplay opt-in.\n";
    return 1;
  }

  AnimationStateMachineComponent invalid;
  invalid.state = "missing";
  invalid.states["idle"] = {};
  invalid.transitions.push_back({.from = "idle", .to = "gone"});
  invalid.blendSpaces["bad"] = {
      .parameterX = "x", .points = {{"gone", 0.0F, 0.0F}}};
  invalid.layers.push_back({.name = "upper", .state = "gone"});
  if (validateAnimationMachine(invalid).size() < 4) {
    std::cerr << "Missing animation references were not diagnosed.\n";
    return 1;
  }
  return 0;
}
