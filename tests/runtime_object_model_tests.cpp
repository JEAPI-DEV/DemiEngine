#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/WorldCommandBuffer.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <iostream>
#include <limits>

using demi::runtime::RuntimeObjectModel;
using demi::runtime::World;
using demi::runtime::WorldCommandBuffer;

namespace {

bool testDerivedFieldMutation() {
  using namespace demi::runtime;
  using Json = nlohmann::json;
  for (bool minimumFirst : {false, true}) {
    Entity entity;
    if (!RuntimeObjectModel::addComponent(entity, "Camera2D", Json::object()))
      return false;
    auto *camera = entity.component<Camera2DComponent>();
    camera->followSpeed = 17;
    camera->followOffset = {8, 9};
    const auto first = minimumFirst ? "bounds_min" : "bounds_max";
    const auto second = minimumFirst ? "bounds_max" : "bounds_min";
    if (!RuntimeObjectModel::setComponentField(entity, "Camera2D", first,
                                               minimumFirst ? Json{-4, -5}
                                                            : Json{6, 7}) ||
        camera->hasBounds ||
        !RuntimeObjectModel::setComponentField(entity, "Camera2D", second,
                                               minimumFirst ? Json{6, 7}
                                                            : Json{-4, -5}) ||
        !camera->hasBounds || camera->boundsMin.x != -4 ||
        camera->boundsMin.y != -5 || camera->boundsMax.x != 6 ||
        camera->boundsMax.y != 7 || camera->followSpeed != 17 ||
        camera->followOffset.y != 9) {
      std::cerr
          << "Camera bounds edits lost their paired bounds/enabling state.\n";
      return false;
    }
    const auto before = entity.serializedComponents.at("Camera2D");
    if (RuntimeObjectModel::setComponentField(entity, "Camera2D", "bounds_min",
                                              "bad") ||
        !camera->hasBounds || camera->boundsMin.x != -4 ||
        entity.serializedComponents.at("Camera2D") != before) {
      std::cerr << "Invalid bounds edit mutated derived or configured state.\n";
      return false;
    }
  }

  Entity entity;
  if (!RuntimeObjectModel::addComponent(entity, "AudioSource",
                                        {{"max_distance", 5}}) ||
      !RuntimeObjectModel::addComponent(entity, "SpotLight",
                                        {{"outer_angle", 40}}) ||
      !RuntimeObjectModel::addComponent(entity, "Environment3D",
                                        {{"fog_end", 90}}) ||
      !RuntimeObjectModel::addComponent(entity, "Camera3D", Json::object()) ||
      !RuntimeObjectModel::addComponent(entity, "CapsuleCollider3D",
                                        Json::object()))
    return false;
  auto *audio = entity.component<AudioSourceComponent>();
  audio->handle = 99;
  audio->volume = 0.25F;
  if (!RuntimeObjectModel::setComponentField(entity, "AudioSource",
                                             "min_distance", 20) ||
      audio->minDistance != 20 || audio->maxDistance != 20 ||
      audio->handle != 99 || audio->volume != 0.25F ||
      !RuntimeObjectModel::setComponentField(entity, "AudioSource",
                                             "min_distance", 2) ||
      audio->minDistance != 2 || audio->maxDistance != 5) {
    std::cerr << "Audio distance edit failed to clamp/recover the configured "
                 "maximum.\n";
    return false;
  }
  auto *light = entity.component<SpotLightComponent>();
  light->intensity = 7;
  if (!RuntimeObjectModel::setComponentField(entity, "SpotLight", "inner_angle",
                                             60) ||
      light->innerAngle != 60 || light->outerAngle != 60 ||
      light->intensity != 7) {
    std::cerr << "Spot-light angle edit broke the cone invariant.\n";
    return false;
  }
  auto *environment = entity.component<Environment3DComponent>();
  environment->shadowDistance = 777;
  if (!RuntimeObjectModel::setComponentField(entity, "Environment3D",
                                             "fog_start", 100) ||
      environment->fogStart != 100 ||
      environment->fogEnd != environment->fogStart + 0.001F ||
      environment->shadowDistance != 777) {
    std::cerr
        << "Fog edit broke the range or reset unrelated shadow settings.\n";
    return false;
  }
  auto *camera = entity.component<Camera3DComponent>();
  camera->fov = 75;
  if (!RuntimeObjectModel::setComponentField(entity, "Camera3D", "near_clip",
                                             600) ||
      camera->nearClip != 600 || camera->farClip != 600 || camera->fov != 75 ||
      !RuntimeObjectModel::setComponentField(entity, "Camera3D", "far_clip",
                                             650) ||
      !RuntimeObjectModel::setComponentField(entity, "Camera3D", "near_clip",
                                             700) ||
      camera->farClip != 700) {
    std::cerr << "Near-plane edit did not clamp default/explicit far planes.\n";
    return false;
  }
  auto *capsule = entity.component<CapsuleCollider3DComponent>();
  capsule->offset = {1, 2, 3};
  if (!RuntimeObjectModel::setComponentField(entity, "CapsuleCollider3D",
                                             "radius", 3) ||
      capsule->radius != 3 || capsule->height != 6 || capsule->offset.z != 3 ||
      !RuntimeObjectModel::setComponentField(entity, "CapsuleCollider3D",
                                             "height", 8) ||
      !RuntimeObjectModel::setComponentField(entity, "CapsuleCollider3D",
                                             "radius", 5) ||
      capsule->height != 10) {
    std::cerr << "Capsule radius edit did not clamp default/explicit height.\n";
    return false;
  }
  return true;
}

bool testSpriteAnimatorFieldMutation() {
  using namespace demi::runtime;
  using Json = nlohmann::json;
  Entity entity;
  const Json clips = {
      {"idle", {{"start_frame", 10}, {"frame_count", 4}, {"fps", 10}}},
      {"run", {{"start_frame", 20}, {"frame_count", 8}, {"fps", 10}}}};
  if (!RuntimeObjectModel::addComponent(entity, "SpriteAnimator2D",
                                        {{"clips", clips}}))
    return false;
  auto *animator = entity.component<SpriteAnimator2DComponent>();
  animator->time = 0.7F;
  animator->playing = false;
  animator->speed = 2;
  animator->previousClip = "blend_source";
  animator->previousFrame = 42;
  animator->blendWeight = 0.25F;
  const auto preserved = [&] {
    return animator->time == 0.7F && !animator->playing &&
           animator->speed == 2 && animator->previousClip == "blend_source" &&
           animator->previousFrame == 42 && animator->blendWeight == 0.25F;
  };
  if (!RuntimeObjectModel::setComponentField(entity, "SpriteAnimator2D", "clip",
                                             "run") ||
      animator->clip != "run" || animator->currentFrame != 27 || !preserved()) {
    std::cerr
        << "Paused clip edit did not resample the retained playback clock.\n";
    return false;
  }
  // The live selected clip may differ from the configured one.
  animator->clip = "idle";
  Json changedClips = clips;
  changedClips["idle"]["start_frame"] = 30;
  if (!RuntimeObjectModel::setComponentField(entity, "SpriteAnimator2D",
                                             "clips", changedClips) ||
      animator->clip != "idle" || animator->currentFrame != 33 ||
      !preserved()) {
    std::cerr
        << "Clip-map edit overwrote live selection or kept a stale frame.\n";
    return false;
  }
  changedClips["idle"]["loop"] = false;
  changedClips["idle"]["frame_count"] = 2;
  if (!RuntimeObjectModel::setComponentField(entity, "SpriteAnimator2D",
                                             "clips", changedClips) ||
      animator->currentFrame != 31 || !preserved()) {
    std::cerr << "Nonlooping clip edit did not clamp the sampled frame.\n";
    return false;
  }
  const auto before = entity.serializedComponents.at("SpriteAnimator2D");
  if (RuntimeObjectModel::setComponentField(entity, "SpriteAnimator2D", "clips",
                                            "bad") ||
      animator->currentFrame != 31 || !preserved() ||
      entity.serializedComponents.at("SpriteAnimator2D") != before)
    return false;
  if (!RuntimeObjectModel::setComponentField(entity, "SpriteAnimator2D",
                                             "speed", 3) ||
      animator->currentFrame != 31 || animator->time != 0.7F ||
      animator->playing)
    return false;

  Entity atlasEntity;
  if (!RuntimeObjectModel::addComponent(atlasEntity, "SpriteAnimator2D",
                                        Json::object()) ||
      !RuntimeObjectModel::setComponentField(
          atlasEntity, "SpriteAnimator2D", "atlas",
          {{"columns", 4}, {"rows", 2}, {"row_names", {"walk", "idle"}}}))
    return false;
  auto *atlas = atlasEntity.component<SpriteAnimator2DComponent>();
  if (atlas->clip != "idle" || atlas->currentFrame != 4)
    return false;
  atlas->playing = false;
  atlas->time = 0.25F;
  if (!RuntimeObjectModel::setComponentField(
          atlasEntity, "SpriteAnimator2D", "atlas",
          {{"columns", 3}, {"rows", 1}, {"row_names", {"jump"}}}) ||
      atlas->clip != "jump" || atlas->currentFrame != 2 ||
      atlas->time != 0.25F || atlas->playing ||
      !RuntimeObjectModel::setComponentField(atlasEntity, "SpriteAnimator2D",
                                             "atlas", Json::object()) ||
      !atlas->clip.empty() || atlas->currentFrame != 0 ||
      atlas->time != 0.25F || atlas->playing) {
    std::cerr << "Atlas edit lost automatic selection/frame invariants.\n";
    return false;
  }
  return true;
}

bool testStateMachineFieldMutation() {
  using namespace demi::runtime;
  using Json = nlohmann::json;
  Entity entity;
  const Json states{{"idle", {{"duration", 4}}},
                    {"run", {{"duration", 2}}},
                    {"old", {{"duration", 3}}}};
  const Json transitions{{"valid", {{"from", "idle"}, {"to", "run"}}},
                         {"wildcard", {{"from", "*"}, {"to", "idle"}}},
                         {"any", {{"from", ""}, {"to", "run"}}},
                         {"removed_from", {{"from", "old"}, {"to", "run"}}},
                         {"removed_to", {{"from", "run"}, {"to", "old"}}}};
  if (!RuntimeObjectModel::addComponent(entity, "AnimationStateMachine",
                                        {{"states", states},
                                         {"initial_state", "run"},
                                         {"transitions", transitions},
                                         {"parameters", {{"speed", 1}}}}))
    return false;
  auto *machine = entity.component<AnimationStateMachineComponent>();
  // Gameplay has selected a state other than the authored initial state.
  machine->state = "idle";
  machine->time = 1.25F;
  machine->normalizedTime = 0.3125F;
  machine->entered = false;
  machine->speed = 2;
  machine->parameters["speed"] = 9;
  machine->triggers.insert("fire");
  machine->activeTransition = {"old", "idle", 2, 0.5F, true};
  machine->blendSamples = {{"idle", 0.75F}, {"old", 0.25F}};
  const auto unrelatedPreserved = [&] {
    return machine->speed == 2 && machine->parameters.at("speed") == 9 &&
           machine->triggers.contains("fire");
  };
  const auto resetPlayback = [&] {
    return machine->time == 0 && machine->normalizedTime == 0 &&
           machine->entered && !machine->activeTransition.active &&
           machine->activeTransition.from.empty() &&
           machine->activeTransition.to.empty() &&
           machine->blendSamples.empty();
  };
  Json survivingStates = states;
  survivingStates.erase("old");
  if (!RuntimeObjectModel::setComponentField(entity, "AnimationStateMachine",
                                             "states", survivingStates) ||
      machine->state != "idle" || machine->time != 1.25F ||
      machine->normalizedTime != 0.3125F || machine->entered ||
      machine->activeTransition.active ||
      !machine->activeTransition.from.empty() ||
      machine->transitions.size() != 3 || machine->blendSamples.size() != 1 ||
      machine->blendSamples.front() !=
          std::pair<std::string, float>{"idle", 0.75F} ||
      !unrelatedPreserved()) {
    std::cerr << "State-map edit reset surviving playback or retained dangling "
                 "references.\n";
    return false;
  }
  machine->activeTransition = {"run", "idle", 2, 0.5F, true};
  if (!RuntimeObjectModel::setComponentField(entity, "AnimationStateMachine",
                                             "states", survivingStates) ||
      machine->state != "idle" || machine->time != 1.25F ||
      !machine->activeTransition.active ||
      machine->activeTransition.elapsed != 0.5F) {
    std::cerr << "State-map edit cancelled a valid active transition.\n";
    return false;
  }
  // Same-state selection is a no-op through both the domain and field paths.
  if (!machine->selectState("idle") ||
      !RuntimeObjectModel::setComponentField(entity, "AnimationStateMachine",
                                             "initial_state", "idle") ||
      machine->time != 1.25F || machine->normalizedTime != 0.3125F ||
      machine->entered || !machine->activeTransition.active ||
      machine->blendSamples.size() != 1 || !unrelatedPreserved())
    return false;
  const auto configuredBefore =
      entity.serializedComponents.at("AnimationStateMachine");
  if (machine->selectState("missing") ||
      RuntimeObjectModel::setComponentField(entity, "AnimationStateMachine",
                                            "initial_state", "missing") ||
      machine->state != "idle" || machine->time != 1.25F ||
      entity.serializedComponents.at("AnimationStateMachine") !=
          configuredBefore)
    return false;
  if (!RuntimeObjectModel::setComponentField(entity, "AnimationStateMachine",
                                             "initial_state", "run") ||
      machine->state != "run" || !resetPlayback() || !unrelatedPreserved()) {
    std::cerr << "Initial-state edit did not use shared playback selection "
                 "semantics.\n";
    return false;
  }
  // Ordinary fields preserve every state-associated runtime value.
  machine->time = 0.75F;
  machine->normalizedTime = 0.375F;
  machine->entered = false;
  machine->activeTransition = {"idle", "run", 2, 0.25F, true};
  machine->blendSamples = {{"run", 1}};
  if (!RuntimeObjectModel::setComponentField(entity, "AnimationStateMachine",
                                             "speed", 2) ||
      !RuntimeObjectModel::setComponentField(entity, "AnimationStateMachine",
                                             "root_motion", true) ||
      !RuntimeObjectModel::setComponentField(entity, "AnimationStateMachine",
                                             "pause_policy", "continue") ||
      machine->time != 0.75F || machine->normalizedTime != 0.375F ||
      machine->entered || !machine->activeTransition.active ||
      machine->activeTransition.elapsed != 0.25F ||
      machine->blendSamples.size() != 1 || !unrelatedPreserved())
    return false;
  if (!RuntimeObjectModel::setComponentField(entity, "AnimationStateMachine",
                                             "transitions", Json::object()) ||
      machine->activeTransition.active ||
      !machine->activeTransition.to.empty() || machine->state != "run" ||
      machine->time != 0.75F || machine->entered)
    return false;

  // A removed live state first falls back to the still-valid configured state.
  if (!machine->selectState("idle"))
    return false;
  machine->time = 3;
  machine->normalizedTime = 0.75F;
  machine->entered = false;
  machine->blendSamples = {{"idle", 1}};
  if (!RuntimeObjectModel::setComponentField(
          entity, "AnimationStateMachine", "states",
          {{"aaa", Json::object()}, {"run", {{"duration", 2}}}}) ||
      machine->state != "run" || !resetPlayback() || !unrelatedPreserved()) {
    std::cerr << "Removing the active state did not select the configured "
                 "fallback.\n";
    return false;
  }
  machine->time = 2;
  machine->entered = false;
  if (!RuntimeObjectModel::setComponentField(
          entity, "AnimationStateMachine", "states",
          {{"zebra", Json::object()}, {"alpha", Json::object()}}) ||
      machine->state != "alpha" || !resetPlayback() || !unrelatedPreserved()) {
    std::cerr << "Removing the configured fallback did not select the lexical "
                 "fallback.\n";
    return false;
  }
  machine->time = 2;
  if (!RuntimeObjectModel::setComponentField(entity, "AnimationStateMachine",
                                             "states", Json::object()) ||
      !machine->state.empty() || !resetPlayback() || !unrelatedPreserved() ||
      !RuntimeObjectModel::setComponentField(entity, "AnimationStateMachine",
                                             "states", states) ||
      machine->state != "run" || !resetPlayback() || !unrelatedPreserved()) {
    std::cerr << "Empty/repopulated state maps failed to recover playback "
                 "selection.\n";
    return false;
  }
  machine->time = 1;
  machine->normalizedTime = 0.5F;
  machine->entered = false;
  machine->activeTransition = {"run", "idle", 1, 0.5F, true};
  machine->blendSamples = {{"run", 1}};
  if (!machine->selectState("idle") || !resetPlayback() ||
      !unrelatedPreserved()) {
    std::cerr << "Shared play-state helper did not reset all state-associated "
                 "data.\n";
    return false;
  }
  return true;
}

} // namespace

int main() {
  using namespace demi::runtime::scene_loading;
  if (!testDerivedFieldMutation() || !testSpriteAnimatorFieldMutation() ||
      !testStateMachineFieldMutation())
    return 1;
  nlohmann::json encodedNumber;
  if (!demi::runtime::runtimeFieldJson(0.05F, encodedNumber) ||
      encodedNumber != nlohmann::json::parse("0.05") ||
      encodedNumber.dump() != "0.05") {
    std::cerr << "Float default exposed double-promotion noise.\n";
    return 1;
  }
  for (float value :
       {std::numeric_limits<float>::denorm_min(),
        std::numeric_limits<float>::min(), 1e-30F, -1e-30F,
        std::nextafter(1.0F, 2.0F), std::numeric_limits<float>::max(), -0.0F}) {
    if (!demi::runtime::runtimeFieldJson(value, encodedNumber) ||
        encodedNumber.get<float>() != value ||
        std::signbit(encodedNumber.get<float>()) != std::signbit(value)) {
      std::cerr << "Float default lost native round-trip precision.\n";
      return 1;
    }
  }
  const double preciseDouble = std::nextafter(0.05, 1.0);
  if (!demi::runtime::runtimeFieldJson(preciseDouble, encodedNumber) ||
      encodedNumber.get<double>() != preciseDouble ||
      demi::runtime::runtimeFieldJson(std::numeric_limits<float>::infinity(),
                                      encodedNumber) ||
      demi::runtime::runtimeFieldJson(std::numeric_limits<float>::quiet_NaN(),
                                      encodedNumber)) {
    std::cerr << "Numeric codec changed double precision or accepted "
                 "non-finite floats.\n";
    return 1;
  }
  nlohmann::json encodedVector, encodedColor, encodedPoints;
  if (!demi::runtime::runtimeFieldJson(demi::runtime::Vec2{0.05F, 0.1F},
                                       encodedVector) ||
      encodedVector != nlohmann::json::parse("[0.05,0.1]") ||
      !demi::runtime::runtimeFieldJson(
          demi::runtime::Color{0.05F, 0.1F, 0.3F, 1.0F}, encodedColor) ||
      encodedColor != nlohmann::json::parse("[0.05,0.1,0.3,1]") ||
      !demi::runtime::runtimeFieldJson(
          std::vector<demi::runtime::Vec3>{{0.05F, 0.1F, 0.3F}},
          encodedPoints) ||
      encodedPoints != nlohmann::json::parse("[[0.05,0.1,0.3]]")) {
    std::cerr << "Vector/color defaults exposed double-promotion noise.\n";
    return 1;
  }
  const auto defaultsFor = [](std::string_view name) -> const nlohmann::json & {
    return componentDefaults(*findComponentDescriptor(name));
  };
  if (defaultsFor("Camera3D").at("fov") != 60 ||
      defaultsFor("Camera3D").at("near_clip") !=
          nlohmann::json::parse("0.05") ||
      defaultsFor("Camera3D").at("perspective") != true ||
      defaultsFor("Camera3D").at("target_offset") !=
          nlohmann::json::array({0, 0, 1}) ||
      defaultsFor("Rigidbody3D").at("mass") != 1 ||
      defaultsFor("Rigidbody3D").at("body_type") != "dynamic" ||
      defaultsFor("Transform3D").at("scale") !=
          nlohmann::json::array({1, 1, 1}) ||
      defaultsFor("Transform2D").at("scale") != nlohmann::json::array({1, 1}) ||
      defaultsFor("Sprite").at("pivot") != nlohmann::json::array({0.5, 0.5}) ||
      defaultsFor("AudioListener").at("primary") != true ||
      defaultsFor("Environment3D").at("relief_image_cache_mb") != 64) {
    std::cerr << "Canonical defaults differ from parsed native defaults.\n";
    return 1;
  }
  if (defaultsFor("AudioSource").at("attenuation") != "inverse" ||
      defaultsFor("AudioSource").at("min_distance") != 1 ||
      defaultsFor("SpotLight").at("inner_angle") != 25 ||
      defaultsFor("Environment3D").at("fog_start") != 80 ||
      defaultsFor("AudioSource").at("voice_stealing") != "oldest" ||
      defaultsFor("AudioSource").at("spatial") != "none" ||
      defaultsFor("AnimationStateMachine").at("pause_policy") != "pause" ||
      defaultsFor("LuaScript").at("properties") != nlohmann::json::object() ||
      defaultsFor("GameplayData").at("values") != nlohmann::json::object() ||
      defaultsFor("MeshRenderer").at("material_properties") !=
          nlohmann::json::object() ||
      defaultsFor("Sprite").at("nine_slice") !=
          nlohmann::json::array({{0, 0}, {0, 0}})) {
    std::cerr
        << "Exceptional field defaults do not use authored representations.\n";
    return 1;
  }
  const auto *spriteAnimator = findComponentDescriptor("SpriteAnimator2D");
  const auto spriteAnimatorSchema = componentSchema(*spriteAnimator);
  demi::runtime::Entity omittedAtlas, explicitAtlas, namedAtlas;
  spriteAnimator->parse(nlohmann::json::object(), omittedAtlas);
  spriteAnimator->parse({{"atlas", nlohmann::json::object()}}, explicitAtlas);
  spriteAnimator->parse({{"atlas", {{"row_names", {"idle"}}}}}, namedAtlas);
  if (spriteAnimatorSchema["properties"]["atlas"].contains("default") ||
      spriteAnimatorSchema["properties"]["clips"].contains("default")) {
    std::cerr
        << "Unsupported atlas/clip authoring acquired a guessed default.\n";
    return 1;
  }
  if (!omittedAtlas.component<demi::runtime::SpriteAnimator2DComponent>()
           ->clips.empty() ||
      !explicitAtlas.component<demi::runtime::SpriteAnimator2DComponent>()
           ->clips.empty() ||
      !namedAtlas.component<demi::runtime::SpriteAnimator2DComponent>()
           ->clips.contains("idle")) {
    std::cerr << "Atlas clips must be generated only for named rows.\n";
    return 1;
  }
  if (spriteAnimator->serialize(omittedAtlas) != nlohmann::json::object() ||
      spriteAnimator->serialize(explicitAtlas) !=
          nlohmann::json({{"atlas", nlohmann::json::object()}})) {
    std::cerr << "Default inspection changed authored atlas presence.\n";
    return 1;
  }
  const auto *relief = findComponentDescriptor("SurfaceRelief3D");
  const auto &reliefDefaults = componentDefaults(*relief);
  if (reliefDefaults.at("height_map") != "" ||
      reliefDefaults.at("depth") != nlohmann::json::parse("0.012") ||
      reliefDefaults.at("uv_scale") != nlohmann::json::array({1, 1}) ||
      !validateComponent(*relief, reliefDefaults).empty() ||
      componentSchema(*relief)["required"] !=
          nlohmann::json::array({"height_map"})) {
    std::cerr << "Required component defaults were not seeded before native "
                 "parsing.\n";
    return 1;
  }
  const auto *camera2D = findComponentDescriptor("Camera2D");
  demi::runtime::Entity omittedBounds, explicitBounds;
  camera2D->parse(nlohmann::json::object(), omittedBounds);
  camera2D->parse({{"bounds_min", {0, 0}}, {"bounds_max", {0, 0}}},
                  explicitBounds);
  if (defaultsFor("Camera2D").contains("bounds_min") ||
      defaultsFor("Camera2D").contains("bounds_max") ||
      omittedBounds.component<demi::runtime::Camera2DComponent>()->hasBounds ||
      !explicitBounds.component<demi::runtime::Camera2DComponent>()
           ->hasBounds) {
    std::cerr << "Optional camera bounds acquired an enabling default.\n";
    return 1;
  }
  for (const auto &descriptor : componentDescriptors()) {
    const auto &defaults = componentDefaults(descriptor);
    if (&defaults != &componentDefaults(descriptor)) {
      std::cerr << "Canonical defaults are not cached per component.\n";
      return 1;
    }
    const auto schema = componentSchema(descriptor);
    for (const auto &field : descriptor.fields) {
      const auto &property = schema["properties"][field.name];
      if (property.contains("default") != defaults.contains(field.name) ||
          (defaults.contains(field.name) &&
           property["default"] != componentFieldDefault(descriptor, field))) {
        std::cerr << "Schema and Inspector defaults differ for "
                  << descriptor.name << '.' << field.name << '\n';
        return 1;
      }
      if (defaults.contains(field.name)) {
        for (const auto &issue : validateComponent(
                 descriptor,
                 {{std::string(field.name), defaults.at(field.name)}})) {
          if (issue.field == field.name) {
            std::cerr << "Invalid advertised default for " << descriptor.name
                      << '.' << field.name << ": " << issue.message << '\n';
            return 1;
          }
        }
      }
    }
  }

  const nlohmann::json audioChoices = {
      {"spatial", {"none", "2d", "3d"}},
      {"attenuation", {"none", "inverse", "linear", "exponential"}},
      {"voice_stealing", {"reject", "oldest", "quietest"}}};
  for (const auto &[field, choices] : audioChoices.items()) {
    for (const auto &choice : choices) {
      demi::runtime::Entity parsedAudio;
      demi::runtime::AudioSourceComponent::parse({{field, choice}},
                                                 parsedAudio);
      nlohmann::json encoded;
      if (!demi::runtime::AudioSourceComponent::serializeField(
              *parsedAudio.component<demi::runtime::AudioSourceComponent>(),
              field, encoded) ||
          encoded != choice) {
        std::cerr << "Audio enum codec does not preserve authored values.\n";
        return 1;
      }
    }
  }

  demi::runtime::Entity liveBody;
  World hierarchy;
  demi::runtime::Entity rootEntity;
  rootEntity.id = "root";
  demi::runtime::Transform3DComponent transform;
  transform.position = {7, 8, 9};
  rootEntity.setComponent(transform);
  rootEntity.serializedComponents["Transform3D"] = "{}";
  hierarchy.entities.push_back(rootEntity);
  if (RuntimeObjectModel::setComponentField(hierarchy, "root", "Transform3D", "parent", "root") ||
      hierarchy.entities[0].component<demi::runtime::Transform3DComponent>()->position.x != 7 ||
      !hierarchy.entities[0].component<demi::runtime::Transform3DComponent>()->parent.empty()) {
    std::cerr << "Rejected reparent reset live transform state.\n";
    return 1;
  }
  liveBody.id = "live";
  demi::runtime::Rigidbody3DComponent body;
  body.velocity = {3, 4, 5};
  body.accumulatedImpulse = {6, 7, 8};
  body.hasKinematicTarget = true;
  liveBody.setComponent(body);
  liveBody.serializedComponents["Rigidbody3D"] = R"({"velocity":[0,0,0]})";
  if (!RuntimeObjectModel::setComponentField(liveBody, "Rigidbody3D", "mass", 9)) {
    std::cerr << "Live body field mutation failed.\n";
    return 1;
  }
  const auto *changedBody = liveBody.component<demi::runtime::Rigidbody3DComponent>();
  if (changedBody->mass != 9 || changedBody->velocity.x != 3 ||
      changedBody->accumulatedImpulse.y != 7 || !changedBody->hasKinematicTarget) {
    std::cerr << "Field mutation overwrote unrelated simulation state.\n";
    return 1;
  }

  nlohmann::json models = nlohmann::json::object();
  if (RuntimeObjectModel::setComponentField(liveBody, "Rigidbody3D", "mass",
          std::numeric_limits<double>::infinity()) ||
      liveBody.component<demi::runtime::Rigidbody3DComponent>()->mass != 9) {
    std::cerr << "Non-finite field mutation was accepted.\n";
    return 1;
  }
  liveBody.setComponent(demi::runtime::MeshInstances3DComponent{});
  if (RuntimeObjectModel::setComponentField(liveBody, "MeshInstances3D", "transforms",
          {{"bad", {{"scale", {0, 1, 1}}}}}) ||
      !liveBody.component<demi::runtime::MeshInstances3DComponent>()->transforms.empty()) {
    std::cerr << "Rejected structured field mutation changed the component.\n";
    return 1;
  }
  demi::runtime::MeshRendererComponent mesh;
  mesh.vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
  mesh.markGeometryChanged();
  const auto originalRevision = mesh.revision;
  liveBody.setComponent(mesh);
  if (!RuntimeObjectModel::setComponentField(liveBody, "MeshRenderer", "vertices",
          nlohmann::json::array({{0, 0, 0}, {4, 0, 0}, {0, 2, 0}})) ||
      liveBody.component<demi::runtime::MeshRendererComponent>()->revision == originalRevision ||
      liveBody.component<demi::runtime::MeshRendererComponent>()->boundsMax.x != 4) {
    std::cerr << "Geometry field edit did not invalidate bounds and mesh revision.\n";
    return 1;
  }
  demi::runtime::AnimationPlayer3DComponent animation;
  animation.time = 3.5F;
  animation.visualClock = 12;
  animation.proceduralPoseRevision = 42;
  liveBody.setComponent(animation);
  if (!RuntimeObjectModel::setComponentField(liveBody, "AnimationPlayer3D", "speed", 2) ||
      liveBody.component<demi::runtime::AnimationPlayer3DComponent>()->time != 3.5F ||
      liveBody.component<demi::runtime::AnimationPlayer3DComponent>()->visualClock != 12 ||
      liveBody.component<demi::runtime::AnimationPlayer3DComponent>()->proceduralPoseRevision != 42) {
    std::cerr << "Animation edit reset playback or procedural pose state.\n";
    return 1;
  }
  demi::runtime::AudioSourceComponent audio;
  audio.handle = 99;
  liveBody.setComponent(audio);
  if (!RuntimeObjectModel::setComponentField(liveBody, "AudioSource", "volume", 0.5) ||
      liveBody.component<demi::runtime::AudioSourceComponent>()->handle != 99) {
    std::cerr << "Audio edit discarded the live voice handle.\n";
    return 1;
  }
  for (int index = 0; index < 40; ++index) {
    models[std::to_string(index)] = "asset://model/" + std::to_string(index);
  }
  demi::runtime::Entity masonry;
  demi::runtime::Masonry3DComponent::parse(
      {{"models", models}, {"size", {1500, 1, 1}}}, masonry);
  if (masonry.component<demi::runtime::Masonry3DComponent>()->models.size() != 40) {
    std::cerr << "Masonry model variants were limited.\n";
    return 1;
  }
  const std::string generatedTypes =
      demi::runtime::scene_loading::generatedLuaComponentTypes();
  if (generatedTypes.find("DemiTransform2DSpec") == std::string::npos ||
      generatedTypes.find("---@field parent? string") == std::string::npos) {
    std::cerr << "Lua component types were not generated from metadata.\n";
    return 1;
  }
  const auto *transformDescriptor =
      demi::runtime::scene_loading::findComponentDescriptor("Transform2D");
  if (transformDescriptor == nullptr ||
      transformDescriptor->fields.front().referenceKind !=
          demi::runtime::ComponentReferenceKind::Entity) {
    std::cerr << "Entity reference metadata was not retained.\n";
    return 1;
  }
  std::string error;
  const auto *environment=demi::runtime::scene_loading::findComponentDescriptor("Environment3D");
  const auto qualitySchema=demi::runtime::scene_loading::componentSchema(*environment);
  if(qualitySchema["properties"]["msaa_samples"]["default"]!=4 ||
     qualitySchema["properties"]["msaa_samples"]["enum"]!=nlohmann::json::array({0,2,4,8,16})) {
    std::cerr<<"MSAA defaults/choices do not match the authored contract.\n";return 1;
  }
  for(int samples:{0,2,4,8,16}) {
    const nlohmann::json component{{"msaa_samples",samples}};
    if(!demi::runtime::scene_loading::validateComponent(*environment,component).empty() ||
       !RuntimeObjectModel::buildEntity({{"id","quality"},{"components",{{"Environment3D",component}}}},error)) {
      std::cerr<<"Valid MSAA mode rejected: "<<error<<'\n';return 1;
    }
  }
  for(const nlohmann::json value:{nlohmann::json(-1),nlohmann::json(1),nlohmann::json(3),nlohmann::json(4.5),nlohmann::json("4")})
    if(RuntimeObjectModel::buildEntity({{"id","bad-quality"},{"components",{{"Environment3D",{{"msaa_samples",value}}}}}},error)) {
      std::cerr<<"Invalid MSAA value accepted.\n";return 1;
    }
  auto parent = RuntimeObjectModel::buildEntity(
      nlohmann::json::parse(R"({
        "id": "parent",
        "tags": ["actor", "friendly"],
        "layer": "gameplay",
        "components": {
          "Transform2D": {"position": [2, 3]}
        }
      })"),
      error);
  if (!parent.has_value() || !parent->tags.contains("actor") ||
      parent->layer != "gameplay") {
    std::cerr << "Reflection-driven entity construction failed: " << error
              << '\n';
    return 1;
  }

  auto luaNumberEntity = RuntimeObjectModel::buildEntity(
      nlohmann::json::parse(R"({
        "id": "lua_numeric_fields",
        "components": {
          "Transform2D": {"position": [0, 0]},
          "Sprite": {"shape": "circle", "sorting_order": 2.0},
          "CircleCollider2D": {"radius": 0.25}
        }
      })"),
      error);
  if (!luaNumberEntity.has_value() ||
      RuntimeObjectModel::buildEntity(
          nlohmann::json::parse(R"({
            "id": "fractional_integer_field",
            "components": {"Sprite": {"sorting_order": 2.5}}
          })"),
          error)
          .has_value()) {
    std::cerr << "Lua-compatible integer field validation failed: " << error
              << '\n';
    return 1;
  }

  World world;
  if (!RuntimeObjectModel::addEntity(world, std::move(*parent)) ||
      RuntimeObjectModel::addEntity(
          world, *demi::runtime::findEntity(world, "parent"))) {
    std::cerr << "Duplicate entity IDs were not rejected.\n";
    return 1;
  }

  auto child = RuntimeObjectModel::buildEntity(
      nlohmann::json::parse(R"({
        "id": "child",
        "tags": ["actor"],
        "layer": "gameplay",
        "components": {
          "Transform2D": {"parent": "parent", "position": [1, 0]},
          "Sprite": {"shape": "rectangle", "size": [1, 1]}
        }
      })"),
      error);
  if (!child.has_value() ||
      !RuntimeObjectModel::addEntity(world, std::move(*child))) {
    std::cerr << "Child entity construction failed: " << error << '\n';
    return 1;
  }

  const auto worldPosition =
      RuntimeObjectModel::worldPosition(world, "child");
  if (!worldPosition.has_value() || (*worldPosition)[0] != 3.0 ||
      (*worldPosition)[1] != 3.0 ||
      RuntimeObjectModel::setParent(world, "parent", "child")) {
    std::cerr << "Hierarchy resolution or cycle rejection failed.\n";
    return 1;
  }

  const auto query = RuntimeObjectModel::query(
      world, {.allComponents = {"Transform2D"},
              .tags = {"actor"},
              .layer = "gameplay"});
  if (query.size() != 2) {
    std::cerr << "Entity component/tag/layer query failed.\n";
    return 1;
  }

  if (!RuntimeObjectModel::setComponentField(
          world, "child", "Sprite", "color",
          nlohmann::json::array({0.25, 0.5, 0.75, 1.0})) ||
      RuntimeObjectModel::componentField(world.entities[1], "Sprite",
                                         "color") !=
          nlohmann::json::array({0.25, 0.5, 0.75, 1.0}) ||
      RuntimeObjectModel::setComponentField(
          world, "child", "Sprite", "unknown", 1)) {
    std::cerr << "Generic component field access or validation failed.\n";
    return 1;
  }

  WorldCommandBuffer commands;
  if (!commands.setEnabled(world, "child", false) ||
      !commands.addComponent(world, "child", "CircleCollider2D",
                             {{"radius", 0.5}}) ||
      !demi::runtime::findEntity(world, "child")->enabled ||
      demi::runtime::findEntity(world, "child")
          ->hasComponent<demi::runtime::CircleCollider2DComponent>()) {
    std::cerr << "World commands were not deferred.\n";
    return 1;
  }
  const auto mutations = commands.flush(world);
  const auto *mutated = demi::runtime::findEntity(world, "child");
  if (mutations.size() != 2 || mutated == nullptr || mutated->enabled ||
      !RuntimeObjectModel::hasComponent(*mutated, "CircleCollider2D")) {
    std::cerr << "Deferred world commands did not flush in order.\n";
    return 1;
  }

  if (!commands.removeComponent(world, "child", "CircleCollider2D") ||
      !commands.destroy(world, "child")) {
    std::cerr << "Remove/destroy commands were rejected unexpectedly.\n";
    return 1;
  }
  (void)commands.flush(world);
  if (demi::runtime::findEntity(world, "child") != nullptr) {
    std::cerr << "Destroy command did not invalidate the stable entity ID.\n";
    return 1;
  }

  auto invalid = RuntimeObjectModel::buildEntity(
      nlohmann::json::parse(
          R"({"id":"bad","components":{"Transform2D":{"bogus":1}}})"),
      error);
  if (invalid.has_value()) {
    std::cerr << "Invalid runtime component fields bypassed validation.\n";
    return 1;
  }
  return 0;
}
