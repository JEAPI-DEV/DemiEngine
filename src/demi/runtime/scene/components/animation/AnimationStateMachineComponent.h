#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace demi::runtime {

struct AnimationStateEvent {
  float time = 0.0F;
  std::string name;
};

struct AnimationState {
  std::string spriteClip;
  std::string modelClipName;
  float duration = 0.0F;
  float speed = 1.0F;
  bool loop = true;
  std::vector<Vec3> rootMotionTrack;
  std::vector<AnimationStateEvent> events;
};

struct AnimationBlendPoint {
  std::string state;
  float x = 0.0F;
  float y = 0.0F;
};

struct AnimationBlendSpace {
  std::string parameterX;
  std::string parameterY;
  std::vector<AnimationBlendPoint> points;
};

struct AnimationLayer {
  std::string name;
  std::string state;
  std::vector<std::string> mask;
  float weight = 1.0F;
  bool additive = false;
};

struct AnimationTransition {
  std::string from;
  std::string to;
  std::string parameter;
  std::string condition = "always";
  float threshold = 0.0F;
  float blendDuration = 0.0F;
};

struct AnimationTransitionState {
  std::string from;
  std::string to;
  float duration = 0.0F;
  float elapsed = 0.0F;
  bool active = false;
};

struct AnimationStateMachineComponent {
  static constexpr std::string_view typeName = "AnimationStateMachine";
  static constexpr bool exposedToLua = true;
  static constexpr ComponentDomain domain = ComponentDomain::Generic;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"states", ComponentFieldType::Object, true},
      ComponentFieldDescriptor{"transitions", ComponentFieldType::Object},
      ComponentFieldDescriptor{"parameters", ComponentFieldType::Object},
      ComponentFieldDescriptor{"blend_spaces", ComponentFieldType::Object},
      ComponentFieldDescriptor{"layers", ComponentFieldType::Object},
      ComponentFieldDescriptor{"initial_state", ComponentFieldType::String},
      ComponentFieldDescriptor{"speed", ComponentFieldType::Number},
      ComponentFieldDescriptor{"root_motion", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"pause_policy", ComponentFieldType::String}};
  static constexpr ComponentEditorMetadata editor{"Animation",
                                                  "Animation State Machine"};
  static void parse(const nlohmann::json &json, Entity &entity);
  static bool serializeField(const AnimationStateMachineComponent &component,
                             std::string_view field, nlohmann::json &out);
  [[nodiscard]] bool selectState(const std::string &name);
  static void copyStates(AnimationStateMachineComponent &destination,
                         const AnimationStateMachineComponent &source);
  static void copyInitialState(AnimationStateMachineComponent &destination,
                              const AnimationStateMachineComponent &source);
  static void afterRuntimeFieldChange(AnimationStateMachineComponent &component,
                                     std::string_view field);

  std::unordered_map<std::string, AnimationState> states;
  std::vector<AnimationTransition> transitions;
  std::unordered_map<std::string, AnimationBlendSpace> blendSpaces;
  std::vector<AnimationLayer> layers;
  std::unordered_map<std::string, float> parameters;
  std::unordered_set<std::string> triggers;
  // Retain the authored request separately from the parser's fallback/live state.
  std::string initialState;
  std::string state;
  float time = 0.0F;
  float speed = 1.0F;
  float normalizedTime = 0.0F;
  bool rootMotion = false;
  bool updateWhenPaused = false;
  AnimationTransitionState activeTransition;
  std::vector<std::pair<std::string, float>> blendSamples;
  bool entered = true;
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<AnimationStateMachineComponent>{
          "states", copyStates,
          RuntimeFieldBinding<AnimationStateMachineComponent>::member<
              &AnimationStateMachineComponent::states>("states").read},
      RuntimeFieldBinding<AnimationStateMachineComponent>::member<
          &AnimationStateMachineComponent::transitions>("transitions"),
      RuntimeFieldBinding<AnimationStateMachineComponent>::member<
          &AnimationStateMachineComponent::parameters>("parameters"),
      RuntimeFieldBinding<AnimationStateMachineComponent>::member<
          &AnimationStateMachineComponent::blendSpaces>("blend_spaces"),
      RuntimeFieldBinding<AnimationStateMachineComponent>::member<
          &AnimationStateMachineComponent::layers>("layers"),
      RuntimeFieldBinding<AnimationStateMachineComponent>{
          "initial_state", copyInitialState,
          RuntimeFieldBinding<AnimationStateMachineComponent>::member<
              &AnimationStateMachineComponent::state>("initial_state").read},
      RuntimeFieldBinding<AnimationStateMachineComponent>::member<
          &AnimationStateMachineComponent::speed>("speed"),
      RuntimeFieldBinding<AnimationStateMachineComponent>::member<
          &AnimationStateMachineComponent::rootMotion>("root_motion"),
      RuntimeFieldBinding<AnimationStateMachineComponent>::member<
          &AnimationStateMachineComponent::updateWhenPaused>("pause_policy")};
};

} // namespace demi::runtime
