#pragma once
#include "demi/runtime/scene/components/ComponentDefinition.h"
#include <cstdint>
#include <string>
namespace demi::runtime {
struct VideoPlayerComponent {
  static constexpr std::string_view typeName = "VideoPlayer";
  static constexpr bool exposedToLua = false;
  static constexpr ComponentDomain domain = ComponentDomain::Generic;
  static void parse(const nlohmann::json &json, Entity &entity);
  std::string clip;
  bool playOnStart = false;
  bool loop = false;
  std::uint64_t handle = 0;
};
} // namespace demi::runtime
