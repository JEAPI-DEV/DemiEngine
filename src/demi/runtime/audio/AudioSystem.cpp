#include "demi/runtime/audio/AudioSystem.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#if DEMI_HAS_MINIAUDIO
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#endif

namespace demi::runtime {

namespace {

[[nodiscard]] bool audioDebugEnabled() {
  const char* value = std::getenv("DEMI_AUDIO_DEBUG");
  return value != nullptr && std::string(value) != "0";
}

[[nodiscard]] double elapsedMs(const std::chrono::steady_clock::time_point start) {
  using namespace std::chrono;
  return duration<double, std::milli>(steady_clock::now() - start).count();
}

[[nodiscard]] std::int64_t steadyNowNs() {
  using namespace std::chrono;
  return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

AudioSystem::AudioSystem() = default;

AudioSystem::~AudioSystem() {
  shutdown();
}

bool AudioSystem::initialize() {
#if !DEMI_HAS_MINIAUDIO
  return false;
#else
  if (initialized_) {
    return true;
  }

  auto engine = std::make_unique<ma_engine>();
  ma_engine_config config = ma_engine_config_init();
  config.periodSizeInFrames = 512;
  config.periodSizeInMilliseconds = 0;
  config.gainSmoothTimeInFrames = 0;
  config.defaultVolumeSmoothTimeInPCMFrames = 0;
  if (audioDebugEnabled()) {
    config.onProcess = &AudioSystem::audioProcessDebug;
    config.pProcessUserData = this;
  }

  if (ma_engine_init(&config, engine.get()) != MA_SUCCESS) {
    std::cerr << "miniaudio engine init failed; audio disabled.\n";
    return false;
  }

  engine_ = engine.release();
  ma_engine_set_volume(static_cast<ma_engine*>(engine_), masterVolume_);
  initialized_ = true;
  if (audioDebugEnabled()) {
    auto* maEngine = static_cast<ma_engine*>(engine_);
    std::cerr << "Audio debug: miniaudio initialized at " << ma_engine_get_sample_rate(maEngine)
              << " Hz, " << ma_engine_get_channels(maEngine) << " channel(s), requested period 512 frames.\n";
    if (maEngine->pDevice != nullptr) {
      const ma_device* device = maEngine->pDevice;
      std::cerr << "Audio debug: backend=" << ma_get_backend_name(device->pContext->backend)
                << ", playback period=" << device->playback.internalPeriodSizeInFrames
                << " frame(s), periods=" << device->playback.internalPeriods << ".\n";
    }
  }
  return true;
#endif
}

void AudioSystem::loadAudioAssets(const AssetRegistry& registry) {
  unloadCachedSounds();
  sounds_.clear();
  for (const AssetManifest& asset : registry.assets) {
    if (asset.type == "AudioClip") {
      sounds_[asset.id] = CachedSound{.path = asset.sourcePath};
#if DEMI_HAS_MINIAUDIO
      if (engine_ == nullptr || !initialized_) {
        continue;
      }

      auto cachedSound = std::make_unique<ma_sound>();
      const auto started = std::chrono::steady_clock::now();
      constexpr ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
      if (ma_sound_init_from_file(static_cast<ma_engine*>(engine_), asset.sourcePath.string().c_str(), flags, nullptr, nullptr, cachedSound.get()) != MA_SUCCESS) {
        std::cerr << "miniaudio failed to preload " << asset.id << " from " << asset.sourcePath.string() << '\n';
        continue;
      }

      if (audioDebugEnabled()) {
        std::cerr << "Audio debug: preloaded " << asset.id << " from " << asset.sourcePath.string()
                  << " in " << elapsedMs(started) << " ms.\n";
      }
      sounds_[asset.id].sound = cachedSound.release();
#endif
    }
  }
}

std::uint64_t AudioSystem::play(const std::string& assetId, const bool loop, const float volume) {
#if !DEMI_HAS_MINIAUDIO
  (void)assetId;
  (void)loop;
  (void)volume;
  return 0;
#else
  if (engine_ == nullptr || !initialized_) {
    std::cerr << "Audio output is not initialized for " << assetId << '\n';
    return 0;
  }

  const auto cached = sounds_.find(assetId);
  if (cached == sounds_.end()) {
    std::cerr << "Audio asset not loaded: " << assetId << '\n';
    return 0;
  }
  if (cached->second.sound == nullptr) {
    std::cerr << "Audio asset was not preloaded: " << assetId << '\n';
    return 0;
  }

  auto sound = std::make_unique<ma_sound>();
  const auto cloneStarted = std::chrono::steady_clock::now();
  if (ma_sound_init_copy(static_cast<ma_engine*>(engine_), static_cast<ma_sound*>(cached->second.sound), MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH, nullptr, sound.get()) != MA_SUCCESS) {
    std::cerr << "miniaudio failed to create playable copy for " << assetId << " from " << cached->second.path.string() << '\n';
    return 0;
  }
  const double cloneMs = elapsedMs(cloneStarted);

  ma_sound_set_looping(sound.get(), loop ? MA_TRUE : MA_FALSE);
  ma_sound_set_volume(sound.get(), std::clamp(volume, 0.0F, 1.0F));
  const auto startStarted = std::chrono::steady_clock::now();
  if (audioDebugEnabled()) {
    pendingDebugStartNs_.store(steadyNowNs(), std::memory_order_release);
  }
  if (ma_sound_start(sound.get()) != MA_SUCCESS) {
    pendingDebugStartNs_.store(0, std::memory_order_release);
    ma_sound_uninit(sound.get());
    std::cerr << "miniaudio failed to start " << assetId << '\n';
    return 0;
  }
  if (audioDebugEnabled()) {
    std::cerr << "Audio debug: play " << assetId << " clone=" << cloneMs
              << " ms, start=" << elapsedMs(startStarted) << " ms.\n";
  }

  std::scoped_lock lock(mutex_);
  const std::uint64_t handle = nextHandle_++;
  playing_.push_back(PlayingSound{.handle = handle, .sound = sound.release()});
  return handle;
#endif
}

bool AudioSystem::stop(const std::uint64_t handle) {
  if (handle == 0) {
    return false;
  }

  std::scoped_lock lock(mutex_);
  const auto found = std::ranges::find_if(playing_, [&](const PlayingSound& playing) { return playing.handle == handle; });
  if (found == playing_.end()) {
    return false;
  }

#if DEMI_HAS_MINIAUDIO
  if (found->sound != nullptr) {
    auto* sound = static_cast<ma_sound*>(found->sound);
    ma_sound_stop(sound);
    ma_sound_uninit(sound);
    delete sound;
  }
#endif
  playing_.erase(found);
  return true;
}

void AudioSystem::setMasterVolume(const float volume) {
  std::scoped_lock lock(mutex_);
  masterVolume_ = std::clamp(volume, 0.0F, 1.0F);
#if DEMI_HAS_MINIAUDIO
  if (engine_ != nullptr) {
    ma_engine_set_volume(static_cast<ma_engine*>(engine_), masterVolume_);
  }
#endif
}

float AudioSystem::masterVolume() const {
  return masterVolume_;
}

void AudioSystem::update() {
#if DEMI_HAS_MINIAUDIO
  std::scoped_lock lock(mutex_);
  std::erase_if(playing_, [](const PlayingSound& playing) {
    if (playing.sound == nullptr) {
      return true;
    }
    auto* sound = static_cast<ma_sound*>(playing.sound);
    if (ma_sound_is_playing(sound)) {
      return false;
    }
    ma_sound_uninit(sound);
    delete sound;
    return true;
  });
#endif
}

void AudioSystem::shutdown() {
#if DEMI_HAS_MINIAUDIO
  {
    std::scoped_lock lock(mutex_);
    for (PlayingSound& playing : playing_) {
      if (playing.sound == nullptr) {
        continue;
      }
      auto* sound = static_cast<ma_sound*>(playing.sound);
      ma_sound_stop(sound);
      ma_sound_uninit(sound);
      delete sound;
      playing.sound = nullptr;
    }
    playing_.clear();
  }

  if (engine_ != nullptr) {
    unloadCachedSounds();
    ma_engine_uninit(static_cast<ma_engine*>(engine_));
    delete static_cast<ma_engine*>(engine_);
    engine_ = nullptr;
  }
#endif
  initialized_ = false;
}

void AudioSystem::unloadCachedSounds() {
#if DEMI_HAS_MINIAUDIO
  for (auto& [id, cached] : sounds_) {
    (void)id;
    if (cached.sound == nullptr) {
      continue;
    }
    auto* sound = static_cast<ma_sound*>(cached.sound);
    ma_sound_uninit(sound);
    delete sound;
    cached.sound = nullptr;
  }
#endif
}

void AudioSystem::audioProcessDebug(void* userData, float* framesOut, const unsigned long long frameCount) {
  (void)framesOut;
  (void)frameCount;
  if (userData == nullptr) {
    return;
  }
  auto* audioSystem = static_cast<AudioSystem*>(userData);
  const std::int64_t startedNs = audioSystem->pendingDebugStartNs_.exchange(0, std::memory_order_acq_rel);
  if (startedNs == 0) {
    return;
  }
  const double callbackMs = static_cast<double>(steadyNowNs() - startedNs) / 1'000'000.0;
  std::cerr << "Audio debug: first mix callback after play in " << callbackMs << " ms.\n";
}

} // namespace demi::runtime
