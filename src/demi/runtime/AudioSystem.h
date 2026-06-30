#pragma once

#include "demi/assets/AssetRegistry.h"

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

class AudioSystem {
public:
  AudioSystem();
  ~AudioSystem();

  AudioSystem(const AudioSystem&) = delete;
  AudioSystem& operator=(const AudioSystem&) = delete;

  [[nodiscard]] bool initialize();
  void loadAudioAssets(const AssetRegistry& registry);
  [[nodiscard]] std::uint64_t play(const std::string& assetId);
  [[nodiscard]] bool stop(std::uint64_t handle);
  void setMasterVolume(float volume);
  [[nodiscard]] float masterVolume() const;
  void update();
  void mixAudio(void* stream, int additionalAmount);
  void shutdown();

private:
  struct SoundData {
    int frequency = 44100;
    int channels = 2;
    std::vector<unsigned char> pcm;
  };

  struct PlayingSound {
    std::uint64_t handle = 0;
    const SoundData* sound = nullptr;
    std::size_t cursor = 0;
    std::uint64_t requestedAt = 0;
    bool firstMixLogged = false;
  };

  bool initialized_ = false;
  std::unordered_map<std::string, SoundData> sounds_;
  std::vector<PlayingSound> playing_;
  std::mutex mutex_;
  void* outputStream_ = nullptr;
  std::uint64_t nextHandle_ = 1;
  float masterVolume_ = 1.0F;
};

} // namespace demi::runtime
