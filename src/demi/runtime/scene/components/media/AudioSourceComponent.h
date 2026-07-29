#pragma once
#include "demi/runtime/audio/AudioTypes.h"
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include <cstdint>
#include <string>
namespace demi::runtime {
struct AudioSourceComponent {
  static constexpr std::string_view typeName = "AudioSource";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::Generic;
  static constexpr std::array fields{
      ComponentFieldDescriptor::assetReference("clip"),
      ComponentFieldDescriptor{"play_on_start", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"loop", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"streaming", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"bus", ComponentFieldType::String},
      ComponentFieldDescriptor{"volume", ComponentFieldType::Number},
      ComponentFieldDescriptor{"pitch", ComponentFieldType::Number},
      ComponentFieldDescriptor{"pan", ComponentFieldType::Number},
      ComponentFieldDescriptor{"spatial", ComponentFieldType::String},
      ComponentFieldDescriptor{"attenuation", ComponentFieldType::String},
      ComponentFieldDescriptor{"min_distance", ComponentFieldType::Number},
      ComponentFieldDescriptor{"max_distance", ComponentFieldType::Number},
      ComponentFieldDescriptor{"rolloff", ComponentFieldType::Number},
      ComponentFieldDescriptor{"doppler", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"fade_in", ComponentFieldType::Number},
      ComponentFieldDescriptor{"concurrency_group", ComponentFieldType::String},
      ComponentFieldDescriptor{"max_voices", ComponentFieldType::Integer},
      ComponentFieldDescriptor{"voice_stealing", ComponentFieldType::String},
      ComponentFieldDescriptor{"pause_with_game", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Media", "Audio Source"};
  static void parse(const nlohmann::json &json, Entity &entity);
  std::string clip;
  bool playOnStart = false;
  bool loop = false;
  bool streaming = false;
  std::string bus = "sfx";
  float volume = 1.0F;
  float pitch = 1.0F;
  float pan = 0.0F;
  AudioSpatialMode spatialMode = AudioSpatialMode::None;
  AudioAttenuation attenuation = AudioAttenuation::Inverse;
  float minDistance = 1.0F;
  float maxDistance = 100.0F;
  float rolloff = 1.0F;
  bool doppler = false;
  float fadeIn = 0.0F;
  std::string concurrencyGroup;
  std::uint32_t maxVoices = 0;
  AudioVoiceStealing voiceStealing = AudioVoiceStealing::Oldest;
  bool pauseWithGame = true;
  std::uint64_t handle = 0;
};
} // namespace demi::runtime
