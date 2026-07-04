#include "demi/runtime/audio/MiniaudioAudioBackend.h"

#include "demi/runtime/audio/AudioClipPreprocess.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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

[[nodiscard]] std::uint32_t audioPeriodFrames() {
  const char* value = std::getenv("DEMI_AUDIO_PERIOD_FRAMES");
  if (value == nullptr) {
    return 512;
  }
  try {
    const int parsed = std::stoi(value);
    if (parsed >= 64 && parsed <= 4096) {
      return static_cast<std::uint32_t>(parsed);
    }
  } catch (const std::exception&) {
  }
  std::cerr << "Audio debug: ignoring invalid DEMI_AUDIO_PERIOD_FRAMES=" << value << '\n';
  return 512;
}

#if DEMI_HAS_MINIAUDIO
[[nodiscard]] std::optional<ma_backend> requestedBackend() {
  const char* value = std::getenv("DEMI_AUDIO_BACKEND");
  if (value == nullptr || std::string(value).empty() || std::string(value) == "default") {
    return std::nullopt;
  }

  const std::string backend = value;
  if (backend == "alsa") {
    return ma_backend_alsa;
  }
  if (backend == "pulse" || backend == "pulseaudio") {
    return ma_backend_pulseaudio;
  }

  std::cerr << "Audio debug: unknown DEMI_AUDIO_BACKEND=" << backend << "; using miniaudio default backend order.\n";
  return std::nullopt;
}

class MiniaudioAudioBackend final : public AudioBackend {
public:
  MiniaudioAudioBackend() = default;
  ~MiniaudioAudioBackend() override { shutdown(); }

  MiniaudioAudioBackend(const MiniaudioAudioBackend&) = delete;
  MiniaudioAudioBackend& operator=(const MiniaudioAudioBackend&) = delete;

  [[nodiscard]] bool initialize() override;
  void loadAudioAssets(const AssetRegistry& registry) override;
  [[nodiscard]] std::uint64_t play(const std::string& assetId, bool loop, float volume) override;
  [[nodiscard]] bool stop(std::uint64_t handle) override;
  void setMasterVolume(float volume) override;
  [[nodiscard]] float masterVolume() const override;
  void update() override;
  void shutdown() override;

private:
  struct CachedSound {
    std::filesystem::path path;
    std::uint64_t trimFrames = 0;
    ma_sound* sound = nullptr;
  };

  struct PlayingSound {
    std::uint64_t handle = 0;
    ma_sound* sound = nullptr;
  };

  void unloadCachedSounds();
  static void audioProcessDebug(void* userData, float* framesOut, unsigned long long frameCount);

  bool initialized_ = false;
  ma_context* context_ = nullptr;
  ma_engine* engine_ = nullptr;
  std::unordered_map<std::string, CachedSound> sounds_;
  std::vector<PlayingSound> playing_;
  std::mutex mutex_;
  std::atomic<std::int64_t> pendingDebugStartNs_ = 0;
  std::uint64_t nextHandle_ = 1;
  float masterVolume_ = 1.0F;
};

bool MiniaudioAudioBackend::initialize() {
  if (initialized_) {
    return true;
  }

  std::unique_ptr<ma_context> context;
  ma_context* contextPtr = nullptr;
  if (const std::optional<ma_backend> backend = requestedBackend()) {
    context = std::make_unique<ma_context>();
    if (ma_context_init(&*backend, 1, nullptr, context.get()) == MA_SUCCESS) {
      contextPtr = context.get();
    } else {
      std::cerr << "miniaudio failed to initialize requested backend " << ma_get_backend_name(*backend) << "; falling back to default backend order.\n";
    }
  }

  auto engine = std::make_unique<ma_engine>();
  ma_engine_config config = ma_engine_config_init();
  config.pContext = contextPtr;
  config.periodSizeInFrames = audioPeriodFrames();
  config.periodSizeInMilliseconds = 0;
  config.gainSmoothTimeInFrames = 0;
  config.defaultVolumeSmoothTimeInPCMFrames = 0;
  if (audioDebugEnabled()) {
    config.onProcess = &MiniaudioAudioBackend::audioProcessDebug;
    config.pProcessUserData = this;
  }

  if (ma_engine_init(&config, engine.get()) != MA_SUCCESS) {
    if (contextPtr != nullptr) {
      ma_context_uninit(contextPtr);
    }
    std::cerr << "miniaudio engine init failed; audio disabled.\n";
    return false;
  }

  context_ = context.release();
  engine_ = engine.release();
  ma_engine_set_volume(engine_, masterVolume_);
  initialized_ = true;
  if (audioDebugEnabled()) {
    std::cerr << "Audio debug: miniaudio initialized at " << ma_engine_get_sample_rate(engine_)
              << " Hz, " << ma_engine_get_channels(engine_) << " channel(s), requested period " << config.periodSizeInFrames << " frames.\n";
    if (engine_->pDevice != nullptr) {
      const ma_device* device = engine_->pDevice;
      std::cerr << "Audio debug: backend=" << ma_get_backend_name(device->pContext->backend)
                << ", playback period=" << device->playback.internalPeriodSizeInFrames
                << " frame(s), periods=" << device->playback.internalPeriods << ".\n";
    }
  }
  return true;
}

void MiniaudioAudioBackend::loadAudioAssets(const AssetRegistry& registry) {
  unloadCachedSounds();
  sounds_.clear();
  for (const AssetManifest& asset : registry.assets) {
    if (asset.type != "AudioClip") {
      continue;
    }

    sounds_[asset.id] = CachedSound{.path = asset.sourcePath};
    if (engine_ == nullptr || !initialized_) {
      continue;
    }

    auto cachedSound = std::make_unique<ma_sound>();
    const auto started = std::chrono::steady_clock::now();
    constexpr ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
    if (ma_sound_init_from_file(engine_, asset.sourcePath.string().c_str(), flags, nullptr, nullptr, cachedSound.get()) != MA_SUCCESS) {
      std::cerr << "miniaudio failed to preload " << asset.id << " from " << asset.sourcePath.string() << '\n';
      continue;
    }

    if (audioDebugEnabled()) {
      std::cerr << "Audio debug: preloaded " << asset.id << " from " << asset.sourcePath.string()
                << " in " << elapsedMs(started) << " ms.\n";
    }
    sounds_[asset.id].trimFrames = detectLeadingSilenceFrames(cachedSound.get());
    if (audioDebugEnabled() && sounds_[asset.id].trimFrames > 0) {
      ma_uint32 sampleRate = 0;
      (void)ma_sound_get_data_format(cachedSound.get(), nullptr, nullptr, &sampleRate, nullptr, 0);
      const double trimMs = sampleRate > 0 ? (static_cast<double>(sounds_[asset.id].trimFrames) * 1000.0 / static_cast<double>(sampleRate)) : 0.0;
      std::cerr << "Audio debug: will trim " << trimMs << " ms of leading silence from " << asset.id << ".\n";
    }
    sounds_[asset.id].sound = cachedSound.release();
  }
}

std::uint64_t MiniaudioAudioBackend::play(const std::string& assetId, const bool loop, const float volume) {
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
  if (ma_sound_init_copy(engine_, cached->second.sound, MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH, nullptr, sound.get()) != MA_SUCCESS) {
    std::cerr << "miniaudio failed to create playable copy for " << assetId << " from " << cached->second.path.string() << '\n';
    return 0;
  }
  const double cloneMs = elapsedMs(cloneStarted);

  ma_sound_set_looping(sound.get(), loop ? MA_TRUE : MA_FALSE);
  ma_sound_set_volume(sound.get(), std::clamp(volume, 0.0F, 1.0F));
  if (cached->second.trimFrames > 0) {
    (void)ma_sound_seek_to_pcm_frame(sound.get(), cached->second.trimFrames);
  }
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
}

bool MiniaudioAudioBackend::stop(const std::uint64_t handle) {
  if (handle == 0) {
    return false;
  }

  std::scoped_lock lock(mutex_);
  const auto found = std::ranges::find_if(playing_, [&](const PlayingSound& playing) { return playing.handle == handle; });
  if (found == playing_.end()) {
    return false;
  }

  if (found->sound != nullptr) {
    ma_sound_stop(found->sound);
    ma_sound_uninit(found->sound);
    delete found->sound;
  }
  playing_.erase(found);
  return true;
}

void MiniaudioAudioBackend::setMasterVolume(const float volume) {
  std::scoped_lock lock(mutex_);
  masterVolume_ = std::clamp(volume, 0.0F, 1.0F);
  if (engine_ != nullptr) {
    ma_engine_set_volume(engine_, masterVolume_);
  }
}

float MiniaudioAudioBackend::masterVolume() const {
  return masterVolume_;
}

void MiniaudioAudioBackend::update() {
  std::scoped_lock lock(mutex_);
  std::erase_if(playing_, [](const PlayingSound& playing) {
    if (playing.sound == nullptr) {
      return true;
    }
    if (ma_sound_is_playing(playing.sound)) {
      return false;
    }
    ma_sound_uninit(playing.sound);
    delete playing.sound;
    return true;
  });
}

void MiniaudioAudioBackend::shutdown() {
  {
    std::scoped_lock lock(mutex_);
    for (PlayingSound& playing : playing_) {
      if (playing.sound == nullptr) {
        continue;
      }
      ma_sound_stop(playing.sound);
      ma_sound_uninit(playing.sound);
      delete playing.sound;
      playing.sound = nullptr;
    }
    playing_.clear();
  }

  if (engine_ != nullptr) {
    unloadCachedSounds();
    ma_engine_uninit(engine_);
    delete engine_;
    engine_ = nullptr;
  }
  if (context_ != nullptr) {
    ma_context_uninit(context_);
    delete context_;
    context_ = nullptr;
  }
  initialized_ = false;
}

void MiniaudioAudioBackend::unloadCachedSounds() {
  for (auto& [id, cached] : sounds_) {
    (void)id;
    if (cached.sound == nullptr) {
      continue;
    }
    ma_sound_uninit(cached.sound);
    delete cached.sound;
    cached.sound = nullptr;
  }
}

void MiniaudioAudioBackend::audioProcessDebug(void* userData, float* framesOut, const unsigned long long frameCount) {
  (void)framesOut;
  (void)frameCount;
  if (userData == nullptr) {
    return;
  }
  auto* audioBackend = static_cast<MiniaudioAudioBackend*>(userData);
  const std::int64_t startedNs = audioBackend->pendingDebugStartNs_.exchange(0, std::memory_order_acq_rel);
  if (startedNs == 0) {
    return;
  }
  const double callbackMs = static_cast<double>(steadyNowNs() - startedNs) / 1'000'000.0;
  std::cerr << "Audio debug: first mix callback after play in " << callbackMs << " ms.\n";
}
#endif

} // namespace

std::unique_ptr<AudioBackend> createMiniaudioAudioBackend() {
#if DEMI_HAS_MINIAUDIO
  return std::make_unique<MiniaudioAudioBackend>();
#else
  return nullptr;
#endif
}

} // namespace demi::runtime
