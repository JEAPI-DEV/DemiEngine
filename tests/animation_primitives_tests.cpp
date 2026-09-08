#include "demi/runtime/animation/AnimationCollision2DSystem.h"
#include "demi/runtime/animation/AnimationStateMachineSystem.h"
#include "demi/runtime/animation/ProceduralIk.h"
#include "demi/runtime/math/VectorMath.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteAnimator2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/animation/AnimationCollision2DComponent.h"
#include "demi/runtime/scene/components/animation/AnimationStateMachineComponent.h"

#include <cmath>
#include <iostream>
#include <nlohmann/json.hpp>

using namespace demi::runtime;

int main() {
  const Vec3 interpolated = math::lerp(Vec3{}, {2, 4, 6}, 0.25F);
  if (interpolated.x != 0.5F || interpolated.y != 1.0F ||
      interpolated.z != 1.5F ||
      std::abs(math::smoothstep(0.0F, 1.0F, 0.5F) - 0.5F) > 0.0001F) {
    std::cerr << "Shared vector interpolation helpers failed.\n";
    return 1;
  }
  const auto ik2D = solveTwoBoneIk2D({0, 0}, {1, 1}, {0, 2}, 1.0F, 1.0F);
  if (!ik2D || !ik2D->reached || std::abs(ik2D->joint.x) > 0.0001F ||
      std::abs(ik2D->joint.y - 1.0F) > 0.0001F) {
    std::cerr << "Two-bone 2D IK did not bend toward its pole.\n";
    return 1;
  }
  const auto ik3D =
      solveTwoBoneIk3D({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, 1.0F, 1.0F);
  if (!ik3D || !ik3D->reached || std::abs(ik3D->joint.x - 0.5F) > 0.0001F ||
      ik3D->joint.y < 0.8F || std::abs(ik3D->joint.z) > 0.0001F) {
    std::cerr << "Two-bone 3D IK did not solve in the pole plane.\n";
    return 1;
  }
  const auto clamped =
      solveTwoBoneIk3D({0, 0, 0}, {4, 0, 0}, {0, 1, 0}, 1.0F, 1.0F);
  if (!clamped || clamped->reached ||
      std::abs(clamped->end.x - 2.0F) > 0.0001F ||
      solveTwoBoneIk3D({}, {}, {}, 0.0F, 1.0F)) {
    std::cerr << "Two-bone IK reach clamping was not deterministic.\n";
    return 1;
  }

  Entity source;
  source.id = "source";
  Transform2DComponent::parse(nlohmann::json::parse(R"({"position":[0,0]})"),
                              source);
  SpriteComponent::parse(nlohmann::json::parse(R"({"flip_x":false})"), source);
  SpriteAnimator2DComponent::parse(nlohmann::json::parse(R"({
    "frame_size":[8,8], "clip":"idle", "clips":{
      "idle":{"frame_count":1}, "attack":{"frame_count":1}
    }
  })"),
                                   source);
  AnimationStateMachineComponent::parse(nlohmann::json::parse(R"({
    "initial_state":"idle",
    "states":{
      "idle":{"sprite_clip":"idle"},
      "attack":{"sprite_clip":"attack","duration":0.3,"loop":false,
                "events":[{"time":0.1,"name":"active"}]}
    },
    "transitions":{
      "begin":{"from":"idle","to":"attack","condition":"trigger","parameter":"attack"},
      "end":{"from":"attack","to":"idle","condition":"finished"}
    }
  })"),
                                        source);
  AnimationCollision2DComponent::parse(nlohmann::json::parse(R"({
    "windows":{"attack":{"start":0.1,"end":0.2,"offset":[0.6,0],
                            "size":[1,1],"mask":"body"}}
  })"),
                                       source);

  Entity target;
  target.id = "target";
  Transform2DComponent::parse(nlohmann::json::parse(R"({"position":[1,0]})"),
                              target);
  AnimationCollision2DComponent::parse(nlohmann::json::parse(R"({
    "receivers":{"main":{"layer":"body","size":[1,2]}}
  })"),
                                       target);

  Entity model;
  model.id = "model";
  AnimationPlayer3DComponent::parse(
      nlohmann::json::parse(R"({"clip_name":"Idle"})"), model);
  AnimationStateMachineComponent::parse(nlohmann::json::parse(R"({
    "initial_state":"run", "states":{"run":{
      "model_clip_name":"Run","speed":1.5}}
  })"),
                                        model);

  World world;
  world.entities.push_back(std::move(source));
  world.entities.push_back(std::move(target));
  world.entities.push_back(std::move(model));
  auto *machine = world.entities[0].component<AnimationStateMachineComponent>();
  machine->triggers.insert("attack");

  AnimationStateMachineSystem stateSystem;
  AnimationCollision2DSystem collisionSystem;
  stateSystem.update(world, 0.0F);
  if (machine->state != "attack" ||
      world.entities[2].component<AnimationPlayer3DComponent>()->clipName !=
          "Run") {
    std::cerr << "Shared animation state adapters failed.\n";
    return 1;
  }
  stateSystem.update(world, 0.12F);
  collisionSystem.update(world);
  if (world.stateAnimationEvents.size() != 1 ||
      world.stateAnimationEvents[0].name != "active" ||
      world.animationCollisionOverlaps.size() != 1 ||
      world.animationCollisionOverlaps[0].targetId != "target") {
    std::cerr << "Animation event or timed collision window failed.\n";
    return 1;
  }
  collisionSystem.update(world);
  if (!world.animationCollisionOverlaps.empty()) {
    std::cerr << "An overlap was reported twice during one window.\n";
    return 1;
  }
  stateSystem.update(world, 0.2F);
  if (machine->state != "idle") {
    std::cerr << "Finished transition failed.\n";
    return 1;
  }
  return 0;
}
