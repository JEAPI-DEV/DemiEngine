#pragma once

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/audio/AudioMixer.h"
#include "demi/runtime/audio/AudioTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace demi::assets {
class AssetResourceLoader;
}

namespace demi::runtime {

class AudioBackend;

class AudioSystem {
public:
  AudioSystem();
  explicit AudioSystem(std::unique_ptr<AudioBackend> backend);
  ~AudioSystem();

  AudioSystem(const AudioSystem &) = delete;
  AudioSystem &operator=(const AudioSystem &) = delete;

  [[nodiscard]] bool initialize();
  void loadAudioAssets(const AssetRegistry &registry);
  [[nodiscard]] std::shared_ptr<assets::AssetResourceLoader>
  createAssetLoader(const AssetRegistry &source);
  [[nodiscard]] std::uint64_t play(const std::string &assetId,
                                   bool loop = false, float volume = 1.0F);
  [[nodiscard]] std::uint64_t play(AudioPlaybackRequest request);
  [[nodiscard]] bool stop(std::uint64_t handle, float fadeOutSeconds = 0.0F);
  [[nodiscard]] bool configure(std::uint64_t handle,
                               const AudioPlaybackRequest &request);
  [[nodiscard]] bool isPlaying(std::uint64_t handle) const;
  void setMasterVolume(float volume);
  [[nodiscard]] float masterVolume() const;
  [[nodiscard]] AudioMixer &mixer();
  [[nodiscard]] const AudioMixer &mixer() const;
  void setListener(const AudioListenerState &listener);
  void setGamePaused(bool paused);
  void setSuspended(bool suspended);
  void update(float deltaTime = 0.0F);
  void shutdown();

private:
  struct Voice {
    std::uint64_t handle = 0;
    std::uint64_t backendHandle = 0;
    std::uint64_t sequence = 0;
    AudioPlaybackRequest request;
    float age = 0.0F;
    float remainingDelay = 0.0F;
    float fadeOutDuration = 0.0F;
    float fadeOutRemaining = 0.0F;
  };

  [[nodiscard]] bool startVoice(Voice &voice);
  void applyVoice(Voice &voice);
  [[nodiscard]] bool enforceConcurrency(const AudioPlaybackRequest &request);

  std::unique_ptr<AudioBackend> backend_;
  AudioMixer mixer_;
  std::unordered_map<std::uint64_t, Voice> voices_;
  std::uint64_t nextHandle_ = 1;
  std::uint64_t nextSequence_ = 1;
  bool initialized_ = false;
  bool gamePaused_ = false;
  bool suspended_ = false;
  std::unordered_set<std::string> streamingAssets_;
};

} // namespace demi::runtime
