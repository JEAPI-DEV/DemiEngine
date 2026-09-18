#include "demi/runtime/animation/AnimationStateMachineSystem.h"
#include "demi/runtime/render/bgfx3d/VisualAnimationBudget3D.h"
#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/components/animation/AnimationStateMachineComponent.h"

#include <iostream>
#include <stdexcept>

using namespace demi::runtime;
using namespace demi::runtime::render;
namespace {
void require(bool value, const char *message) {
  if (!value)
    throw std::runtime_error(message);
}
World makeWorld(float rate) {
  World world;
  Entity actor;
  actor.id = "actor";
  actor.setComponent(Transform3DComponent{});
  AnimationPlayer3DComponent::parse(
      {{"visual_update_rate", rate}, {"visual_update_distance", 10}}, actor);
  AnimationStateMachineComponent::parse(nlohmann::json::parse(R"({
    "initial_state":"walk","root_motion":true,
    "states":{"walk":{"model_clip_name":"Walk","duration":1,"loop":true,
      "events":[{"time":0.25,"name":"step"}],"root_motion_track":[[0,0,0],[1,0,0]]}}
  })"),
                                        actor);
  world.entities.push_back(std::move(actor));
  return world;
}
} // namespace
int main() {
  try {
    auto *descriptor =
        scene_loading::findComponentDescriptor("AnimationPlayer3D");
    require(descriptor != nullptr, "component metadata missing");
    nlohmann::json authored{{"visual_update_rate", 15},
                            {"visual_update_distance", 30}};
    require(scene_loading::validateComponent(*descriptor, authored).empty(),
            "valid budget rejected");
    require(!scene_loading::validateComponent(*descriptor,
                                              {{"visual_update_rate", -1}})
                 .empty(),
            "negative rate accepted");
    require(!scene_loading::validateComponent(*descriptor,
                                              {{"visual_update_rate", 241}})
                 .empty(),
            "oversized rate accepted");
    require(!scene_loading::validateComponent(*descriptor,
                                              {{"visual_update_distance", -1}})
                 .empty(),
            "negative distance accepted");
    Entity parsed;
    descriptor->parse(authored, parsed);
    require(descriptor->serialize(parsed) == authored,
            "authored budget did not round-trip");
    auto schema = scene_loading::componentSchema(*descriptor);
    require(schema["properties"]["visual_update_rate"]["maximum"] == 240,
            "schema disagrees with rate bounds");
    require(scene_loading::componentDefaults(
                *descriptor)["visual_update_rate"] == 0,
            "default must preserve full rate");
    require(!descriptor->serialize(parsed).contains("visualClock"),
            "runtime clock leaked to source");
    bool editorField = false;
    for (const auto &field : descriptor->fields)
      if (field.name == "visual_update_rate")
        editorField = field.editorVisible;
    require(editorField, "budget is not exposed to the shared inspector");

    for (int entity = 0; entity < 32; ++entity) {
      VisualAnimationSample3D sample{0, 0, 15};
      int updates = 0;
      for (int frame = 1; frame <= 240; ++frame) {
        const double clock = double(frame) / 240;
        const float time =
            float(clock * 2); // Sampling frequency is not clip speed.
        if (visualAnimationSampleDue(sample, clock, time, 15,
                                     std::to_string(entity))) {
          sample = {clock, time, 15};
          ++updates;
        }
      }
      require(updates == 15, "visual updates are not rate bounded/staggered");
      require(visualAnimationSampleDue(sample, 1.001, 2.002F, 0, "actor"),
              "near pose was deferred");
      require(visualAnimationSampleDue(sample, 0, 0, 15, "actor"),
              "clock reset was deferred");
      require(visualAnimationSampleDue(sample, 1.001, 0, 15, "actor"),
              "backward seek was deferred");
      require(visualAnimationSampleDue(sample, 1.001, 2.002F, 30, "actor"),
              "policy change was deferred");
    }
    auto full = makeWorld(0), budget = makeWorld(15);
    AnimationStateMachineSystem system;
    int events = 0;
    for (int frame = 0; frame < 480; ++frame) {
      const float dt = frame >= 120 && frame < 180 ? 0 : 1.0F / 120;
      system.update(full, dt, 1.0F / 120);
      system.update(budget, dt, 1.0F / 120);
      const auto *a = full.entities[0].component<AnimationPlayer3DComponent>();
      const auto *b =
          budget.entities[0].component<AnimationPlayer3DComponent>();
      require(a->time == b->time && a->visualClock == b->visualClock,
              "budget changed gameplay clock");
      require(full.stateAnimationEvents.size() ==
                  budget.stateAnimationEvents.size(),
              "budget changed events");
      events += static_cast<int>(budget.stateAnimationEvents.size());
      require(
          full.entities[0].component<Transform3DComponent>()->position.x ==
              budget.entities[0].component<Transform3DComponent>()->position.x,
          "budget changed root motion");
    }
    require(events >= 3, "event test did not exercise repeated events");
    std::cout << "Visual budget metadata, rate, pause, events and root motion "
                 "passed.\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
