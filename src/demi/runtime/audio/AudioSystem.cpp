#include "demi/runtime/audio/AudioSystem.h"

#include "demi/runtime/audio/AudioBackend.h"
#include "demi/runtime/audio/MiniaudioAudioBackend.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <vector>

namespace demi::runtime {

namespace {

class NullAudioBackend final : public AudioBackend {
public:
  [[nodiscard]] bool initialize() override { return false; }
  void loadAudioAssets(const AssetRegistry& registry) override { (void)registry; }
  [[nodiscard]] std::uint64_t play(const std::string& assetId, bool loop,
                                   bool streaming) override {
    (void)assetId;
    (void)loop;
    (void)streaming;
    return 0;
  }
  [[nodiscard]] bool stop(std::uint64_t handle) override {
    (void)handle;
    return false;
  }
  [[nodiscard]] bool
  configure(const std::uint64_t handle,
            const AudioBackendVoiceParameters &parameters) override {
    (void)handle;
    (void)parameters;
    return false;
  }
  [[nodiscard]] bool isPlaying(const std::uint64_t handle) const override {
    (void)handle;
    return false;
  }
  void setListener(const AudioListenerState &listener) override {
    (void)listener;
  }
  void setDeviceSuspended(const bool suspended) override { (void)suspended; }
  void update() override {}
  void shutdown() override {}
};

[[nodiscard]] std::unique_ptr<AudioBackend> createAudioBackend() {
  std::unique_ptr<AudioBackend> backend = createMiniaudioAudioBackend();
  if (backend != nullptr) {
    return backend;
  }
  return std::make_unique<NullAudioBackend>();
}

} // namespace

AudioSystem::AudioSystem()
  : backend_(createAudioBackend()) {
}

AudioSystem::AudioSystem(std::unique_ptr<AudioBackend> backend)
    : backend_(std::move(backend)) {
  if (backend_ == nullptr)
    backend_ = std::make_unique<NullAudioBackend>();
}

AudioSystem::~AudioSystem() {
  shutdown();
}

bool AudioSystem::initialize() {
  initialized_ = backend_ != nullptr && backend_->initialize();
  return initialized_;
}

void AudioSystem::loadAudioAssets(const AssetRegistry& registry) {
  streamingAssets_.clear();
  for (const AssetManifest &asset : registry.assets) {
    if (asset.type != "AudioClip")
      continue;
    const nlohmann::json settings =
        nlohmann::json::parse(asset.settingsJson, nullptr, false);
    if (settings.is_object() && settings.value("streaming", false))
      streamingAssets_.insert(asset.id);
  }
  if (backend_ != nullptr) {
    backend_->loadAudioAssets(registry);
  }
}

std::uint64_t AudioSystem::play(const std::string& assetId, const bool loop, const float volume) {
  return play(AudioPlaybackRequest{
      .assetId = assetId,
      .concurrencyGroup = {},
      .loop = loop,
      .volume = volume,
      .position = {},
      .velocity = {}});
}

bool AudioSystem::enforceConcurrency(const AudioPlaybackRequest &request) {
  if (request.concurrencyGroup.empty() || request.maxVoices == 0)
    return true;
  std::vector<Voice *> matching;
  for (auto &[unused, voice] : voices_) {
    (void)unused;
    if (voice.request.concurrencyGroup == request.concurrencyGroup)
      matching.push_back(&voice);
  }
  if (matching.size() < request.maxVoices)
    return true;
  if (request.voiceStealing == AudioVoiceStealing::Reject)
    return false;
  const auto selected =
      request.voiceStealing == AudioVoiceStealing::Quietest
          ? std::ranges::min_element(
                matching, {}, [](const Voice *voice) {
                  return voice->request.volume;
                })
          : std::ranges::min_element(
                matching, {}, [](const Voice *voice) {
                  return voice->sequence;
                });
  if (selected == matching.end())
    return false;
  (void)stop((*selected)->handle);
  return true;
}

std::uint64_t AudioSystem::play(AudioPlaybackRequest request) {
  if (request.assetId.empty() || !enforceConcurrency(request))
    return 0;
  request.volume = std::max(request.volume, 0.0F);
  request.pitch = std::max(request.pitch, 0.01F);
  request.pan = std::clamp(request.pan, -1.0F, 1.0F);
  request.delaySeconds = std::max(request.delaySeconds, 0.0F);
  request.fadeInSeconds = std::max(request.fadeInSeconds, 0.0F);
  request.minDistance = std::max(request.minDistance, 0.001F);
  request.maxDistance = std::max(request.maxDistance, request.minDistance);
  request.rolloff = std::max(request.rolloff, 0.0F);
  request.streaming =
      request.streaming || streamingAssets_.contains(request.assetId);

  Voice voice{.handle = nextHandle_++,
              .sequence = nextSequence_++,
              .request = std::move(request)};
  voice.remainingDelay = voice.request.delaySeconds;
  const std::uint64_t handle = voice.handle;
  auto [found, inserted] = voices_.emplace(handle, std::move(voice));
  if (!inserted)
    return 0;
  if (found->second.remainingDelay <= 0.0F && !startVoice(found->second)) {
    voices_.erase(found);
    return 0;
  }
  return handle;
}

bool AudioSystem::startVoice(Voice &voice) {
  if (!initialized_ || backend_ == nullptr)
    return false;
  voice.backendHandle = backend_->play(voice.request.assetId,
                                      voice.request.loop,
                                      voice.request.streaming);
  if (voice.backendHandle == 0)
    return false;
  applyVoice(voice);
  return true;
}

void AudioSystem::applyVoice(Voice &voice) {
  if (voice.backendHandle == 0 || backend_ == nullptr)
    return;
  float fade = 1.0F;
  if (voice.request.fadeInSeconds > 0.0F)
    fade = std::min(fade, std::clamp(
                              voice.age / voice.request.fadeInSeconds, 0.0F,
                              1.0F));
  if (voice.fadeOutDuration > 0.0F)
    fade = std::min(fade,
                    std::clamp(voice.fadeOutRemaining /
                                   voice.fadeOutDuration,
                               0.0F, 1.0F));
  const AudioBackendVoiceParameters parameters{
      .gain = voice.request.volume *
              mixer_.effectiveGain(voice.request.bus) * fade,
      .pitch = voice.request.pitch,
      .pan = voice.request.pan,
      .spatialMode = voice.request.spatialMode,
      .attenuation = voice.request.attenuation,
      .position = voice.request.position,
      .velocity = voice.request.velocity,
      .minDistance = voice.request.minDistance,
      .maxDistance = voice.request.maxDistance,
      .rolloff = voice.request.rolloff,
      .doppler = voice.request.doppler,
      .paused = suspended_ || mixer_.effectivelyPaused(voice.request.bus) ||
                (gamePaused_ && voice.request.pauseWithGame)};
  (void)backend_->configure(voice.backendHandle, parameters);
}

bool AudioSystem::stop(const std::uint64_t handle,
                       const float fadeOutSeconds) {
  const auto found = voices_.find(handle);
  if (found == voices_.end())
    return false;
  if (fadeOutSeconds > 0.0F && found->second.backendHandle != 0) {
    found->second.fadeOutDuration = fadeOutSeconds;
    found->second.fadeOutRemaining = fadeOutSeconds;
    return true;
  }
  const bool stopped =
      found->second.backendHandle == 0 || backend_ == nullptr ||
      backend_->stop(found->second.backendHandle);
  voices_.erase(found);
  return stopped;
}

bool AudioSystem::configure(const std::uint64_t handle,
                            const AudioPlaybackRequest &request) {
  const auto found = voices_.find(handle);
  if (found == voices_.end())
    return false;
  const std::string assetId = found->second.request.assetId;
  const bool loop = found->second.request.loop;
  const bool streaming = found->second.request.streaming;
  found->second.request = request;
  found->second.request.assetId = assetId;
  found->second.request.loop = loop;
  found->second.request.streaming = streaming;
  applyVoice(found->second);
  return true;
}

bool AudioSystem::isPlaying(const std::uint64_t handle) const {
  const auto found = voices_.find(handle);
  if (found == voices_.end())
    return false;
  if (found->second.backendHandle == 0 || suspended_ ||
      mixer_.effectivelyPaused(found->second.request.bus) ||
      (gamePaused_ && found->second.request.pauseWithGame))
    return true;
  return backend_ != nullptr &&
         backend_->isPlaying(found->second.backendHandle);
}

void AudioSystem::setMasterVolume(const float volume) {
  (void)mixer_.setVolume("master", std::clamp(volume, 0.0F, 1.0F));
}

float AudioSystem::masterVolume() const {
  return mixer_.volume("master");
}

AudioMixer &AudioSystem::mixer() { return mixer_; }

const AudioMixer &AudioSystem::mixer() const { return mixer_; }

void AudioSystem::setListener(const AudioListenerState &listener) {
  if (backend_ != nullptr)
    backend_->setListener(listener);
}

void AudioSystem::setGamePaused(const bool paused) { gamePaused_ = paused; }

void AudioSystem::setSuspended(const bool suspended) {
  if (suspended_ == suspended)
    return;
  suspended_ = suspended;
  if (backend_ != nullptr)
    backend_->setDeviceSuspended(suspended);
}

void AudioSystem::update(const float deltaTime) {
  const float dt = std::max(deltaTime, 0.0F);
  mixer_.update(dt);
  std::vector<std::uint64_t> finished;
  for (auto &[handle, voice] : voices_) {
    if (voice.remainingDelay > 0.0F) {
      voice.remainingDelay -= dt;
      if (voice.remainingDelay <= 0.0F && !startVoice(voice))
        finished.push_back(handle);
      continue;
    }
    voice.age += dt;
    if (voice.fadeOutDuration > 0.0F) {
      voice.fadeOutRemaining -= dt;
      if (voice.fadeOutRemaining <= 0.0F) {
        if (backend_ != nullptr)
          (void)backend_->stop(voice.backendHandle);
        finished.push_back(handle);
        continue;
      }
    }
    const bool paused =
        suspended_ || mixer_.effectivelyPaused(voice.request.bus) ||
        (gamePaused_ && voice.request.pauseWithGame);
    if (!paused && voice.backendHandle != 0 && backend_ != nullptr &&
        !backend_->isPlaying(voice.backendHandle) && !voice.request.loop) {
      finished.push_back(handle);
      continue;
    }
    applyVoice(voice);
  }
  for (const std::uint64_t handle : finished)
    voices_.erase(handle);
  if (backend_ != nullptr)
    backend_->update();
}

void AudioSystem::shutdown() {
  if (backend_ != nullptr) {
    voices_.clear();
    backend_->shutdown();
  }
  initialized_ = false;
}

} // namespace demi::runtime
