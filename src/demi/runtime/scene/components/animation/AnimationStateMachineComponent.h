#pragma once

#include "demi/runtime/scene/components/ComponentDefinition.h"

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
  int modelClip = -1;
  std::string modelClipName;
  float duration = 0.0F;
  float speed = 1.0F;
  bool loop = true;
  std::vector<AnimationStateEvent> events;
};

struct AnimationTransition {
  std::string from;
  std::string to;
  std::string parameter;
  std::string condition = "always";
  float threshold = 0.0F;
};

struct AnimationStateMachineComponent {
  static constexpr std::string_view typeName = "AnimationStateMachine";
  static constexpr bool exposedToLua = true;
  static constexpr ComponentDomain domain = ComponentDomain::Generic;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"states", ComponentFieldType::Object, true},
      ComponentFieldDescriptor{"transitions", ComponentFieldType::Object},
      ComponentFieldDescriptor{"parameters", ComponentFieldType::Object},
      ComponentFieldDescriptor{"initial_state", ComponentFieldType::String}};
  static constexpr ComponentEditorMetadata editor{"Animation",
                                                  "Animation State Machine"};
  static void parse(const nlohmann::json &json, Entity &entity);

  std::unordered_map<std::string, AnimationState> states;
  std::vector<AnimationTransition> transitions;
  std::unordered_map<std::string, float> parameters;
  std::unordered_set<std::string> triggers;
  std::string state;
  float time = 0.0F;
  bool entered = true;
};

} // namespace demi::runtime
