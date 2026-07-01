#include "demi/runtime/AudioSystem.h"

#include <algorithm>
#include <iostream>
#include <memory>

#if DEMI_HAS_MINIAUDIO
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
#endif

namespace demi::runtime {

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
  if (ma_engine_init(nullptr, engine.get()) != MA_SUCCESS) {
    std::cerr << "miniaudio engine init failed; audio disabled.\n";
    return false;
  }

  engine_ = engine.release();
  ma_engine_set_volume(static_cast<ma_engine*>(engine_), masterVolume_);
  initialized_ = true;
  return true;
#endif
}

void AudioSystem::loadAudioAssets(const AssetRegistry& registry) {
  sounds_.clear();
  for (const AssetManifest& asset : registry.assets) {
    if (asset.type == "AudioClip") {
      sounds_[asset.id] = asset.sourcePath;
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

  const auto soundPath = sounds_.find(assetId);
  if (soundPath == sounds_.end()) {
    std::cerr << "Audio asset not loaded: " << assetId << '\n';
    return 0;
  }

  auto sound = std::make_unique<ma_sound>();
  if (ma_sound_init_from_file(static_cast<ma_engine*>(engine_), soundPath->second.string().c_str(), 0, nullptr, nullptr, sound.get()) != MA_SUCCESS) {
    std::cerr << "miniaudio failed to load " << assetId << " from " << soundPath->second.string() << '\n';
    return 0;
  }

  ma_sound_set_looping(sound.get(), loop ? MA_TRUE : MA_FALSE);
  ma_sound_set_volume(sound.get(), std::clamp(volume, 0.0F, 1.0F));
  if (ma_sound_start(sound.get()) != MA_SUCCESS) {
    ma_sound_uninit(sound.get());
    std::cerr << "miniaudio failed to start " << assetId << '\n';
    return 0;
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
    ma_engine_uninit(static_cast<ma_engine*>(engine_));
    delete static_cast<ma_engine*>(engine_);
    engine_ = nullptr;
  }
#endif
  initialized_ = false;
}

} // namespace demi::runtime
