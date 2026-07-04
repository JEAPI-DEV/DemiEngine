#pragma once

#include "demi/assets/AssetRegistry.h"

#include <cstdint>
#include <string>

namespace demi::runtime {

class AudioBackend {
public:
  virtual ~AudioBackend() = default;

  [[nodiscard]] virtual bool initialize() = 0;
  virtual void loadAudioAssets(const AssetRegistry& registry) = 0;
  [[nodiscard]] virtual std::uint64_t play(const std::string& assetId, bool loop, float volume) = 0;
  [[nodiscard]] virtual bool stop(std::uint64_t handle) = 0;
  virtual void setMasterVolume(float volume) = 0;
  [[nodiscard]] virtual float masterVolume() const = 0;
  virtual void update() = 0;
  virtual void shutdown() = 0;
};

} // namespace demi::runtime
