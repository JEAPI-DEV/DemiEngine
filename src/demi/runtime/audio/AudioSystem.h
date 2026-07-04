#pragma once

#include "demi/assets/AssetRegistry.h"

#include <cstdint>
#include <memory>
#include <string>

namespace demi::runtime {

class AudioBackend;

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
  std::unique_ptr<AudioBackend> backend_;
};

} // namespace demi::runtime
