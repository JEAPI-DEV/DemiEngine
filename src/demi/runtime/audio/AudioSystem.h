#pragma once

#include "demi/assets/AssetRegistry.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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
  [[nodiscard]] std::uint64_t play(const std::string& assetId, bool loop = false, float volume = 1.0F);
  [[nodiscard]] bool stop(std::uint64_t handle);
  void setMasterVolume(float volume);
  [[nodiscard]] float masterVolume() const;
  void update();
  void shutdown();

private:
  struct CachedSound {
    std::filesystem::path path;
    void* sound = nullptr;
  };

  struct PlayingSound {
    std::uint64_t handle = 0;
    void* sound = nullptr;
  };

  void unloadCachedSounds();
  static void audioProcessDebug(void* userData, float* framesOut, unsigned long long frameCount);

  bool initialized_ = false;
  void* engine_ = nullptr;
  std::unordered_map<std::string, CachedSound> sounds_;
  std::vector<PlayingSound> playing_;
  std::mutex mutex_;
  std::atomic<std::int64_t> pendingDebugStartNs_ = 0;
  std::uint64_t nextHandle_ = 1;
  float masterVolume_ = 1.0F;
};

} // namespace demi::runtime
