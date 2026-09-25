#pragma once
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include "demi/runtime/scene/components/RuntimeFieldBinding.h"
#include <cstdint>
#include <string>
namespace demi::runtime {
struct VideoPlayerComponent {
  static constexpr std::string_view typeName = "VideoPlayer";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::Generic;
  static constexpr std::array fields{
      ComponentFieldDescriptor::assetReference("clip"),
      ComponentFieldDescriptor{"play_on_start", ComponentFieldType::Boolean},
      ComponentFieldDescriptor{"loop", ComponentFieldType::Boolean}};
  static constexpr ComponentEditorMetadata editor{"Media", "Video Player"};
  static void parse(const nlohmann::json &json, Entity &entity);
  std::string clip;
  bool playOnStart = false;
  bool loop = false;
  std::uint64_t handle = 0;
  static constexpr std::array runtimeFields{
      RuntimeFieldBinding<VideoPlayerComponent>::member<
          &VideoPlayerComponent::clip>("clip"),
      RuntimeFieldBinding<VideoPlayerComponent>::member<
          &VideoPlayerComponent::playOnStart>("play_on_start"),
      RuntimeFieldBinding<VideoPlayerComponent>::member<
          &VideoPlayerComponent::loop>("loop")};
};
} // namespace demi::runtime
