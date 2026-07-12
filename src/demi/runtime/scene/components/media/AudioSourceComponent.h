#pragma once
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include <cstdint>
#include <string>
namespace demi::runtime {
struct AudioSourceComponent {
  static constexpr std::string_view typeName = "AudioSource";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::Generic;
  static constexpr std::array fields{
      ComponentFieldDescriptor{"clip", ComponentFieldType::String},
      ComponentFieldDescriptor{"play_on_start", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"loop", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"volume", ComponentFieldType::Number}};
  static constexpr ComponentEditorMetadata editor{"Media", "Audio Source"};
  static void parse(const nlohmann::json &json, Entity &entity);
  std::string clip;
  bool playOnStart = false;
  bool loop = false;
  float volume = 1.0F;
  std::uint64_t handle = 0;
};
} // namespace demi::runtime
