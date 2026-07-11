#pragma once

#include <cstdint>
#include <string>

namespace demi::runtime {

struct AudioSourceComponent {
  std::string clip;
  bool playOnStart = false;
  bool loop = false;
  float volume = 1.0F;
  std::uint64_t handle = 0;
};

struct AudioListenerComponent {
  bool primary = true;
};

struct VideoPlayerComponent {
  std::string clip;
  bool playOnStart = false;
  bool loop = false;
  std::uint64_t handle = 0;
};

} // namespace demi::runtime
