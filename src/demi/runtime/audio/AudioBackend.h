#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/audio/AudioTypes.h"

#include <cstdint>
#include <string>

namespace demi::runtime {

class AudioBackend {
public:
  virtual ~AudioBackend() = default;

  [[nodiscard]] virtual bool initialize() = 0;
  virtual void loadAudioAssets(const AssetRegistry& registry) = 0;
  [[nodiscard]] virtual std::uint64_t
  play(const std::string &assetId, bool loop, bool streaming) = 0;
  [[nodiscard]] virtual bool stop(std::uint64_t handle) = 0;
  [[nodiscard]] virtual bool
  configure(std::uint64_t handle,
            const AudioBackendVoiceParameters &parameters) = 0;
  [[nodiscard]] virtual bool isPlaying(std::uint64_t handle) const = 0;
  virtual void setListener(const AudioListenerState &listener) = 0;
  virtual void setDeviceSuspended(bool suspended) = 0;
  virtual void update() = 0;
  virtual void shutdown() = 0;
};

} // namespace demi::runtime
