#include "demi/runtime/audio/AudioSystem.h"

#include "demi/runtime/audio/AudioBackend.h"
#include "demi/runtime/audio/MiniaudioAudioBackend.h"

#include <algorithm>
#include <memory>

namespace demi::runtime {

namespace {

class NullAudioBackend final : public AudioBackend {
public:
  [[nodiscard]] bool initialize() override { return false; }
  void loadAudioAssets(const AssetRegistry& registry) override { (void)registry; }
  [[nodiscard]] std::uint64_t play(const std::string& assetId, bool loop, float volume) override {
    (void)assetId;
    (void)loop;
    (void)volume;
    return 0;
  }
  [[nodiscard]] bool stop(std::uint64_t handle) override {
    (void)handle;
    return false;
  }
  void setMasterVolume(const float volume) override { masterVolume_ = std::clamp(volume, 0.0F, 1.0F); }
  [[nodiscard]] float masterVolume() const override { return masterVolume_; }
  void update() override {}
  void shutdown() override {}

private:
  float masterVolume_ = 1.0F;
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

AudioSystem::~AudioSystem() {
  shutdown();
}

bool AudioSystem::initialize() {
  return backend_ != nullptr && backend_->initialize();
}

void AudioSystem::loadAudioAssets(const AssetRegistry& registry) {
  if (backend_ != nullptr) {
    backend_->loadAudioAssets(registry);
  }
}

std::uint64_t AudioSystem::play(const std::string& assetId, const bool loop, const float volume) {
  return backend_ != nullptr ? backend_->play(assetId, loop, volume) : 0;
}

bool AudioSystem::stop(const std::uint64_t handle) {
  return backend_ != nullptr && backend_->stop(handle);
}

void AudioSystem::setMasterVolume(const float volume) {
  if (backend_ != nullptr) {
    backend_->setMasterVolume(volume);
  }
}

float AudioSystem::masterVolume() const {
  return backend_ != nullptr ? backend_->masterVolume() : 1.0F;
}

void AudioSystem::update() {
  if (backend_ != nullptr) {
    backend_->update();
  }
}

void AudioSystem::shutdown() {
  if (backend_ != nullptr) {
    backend_->shutdown();
  }
}

} // namespace demi::runtime
