#include "demi/runtime/audio/AudioSystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
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

[[nodiscard]] float pcmSampleAsFloat(const unsigned char* sample, const ma_format format) {
  switch (format) {
  case ma_format_u8:
    return (static_cast<float>(*sample) - 128.0F) / 128.0F;
  case ma_format_s16: {
    std::int16_t value = 0;
    std::memcpy(&value, sample, sizeof(value));
    return static_cast<float>(value) / 32768.0F;
  }
  case ma_format_s24: {
    std::int32_t value = (static_cast<std::int32_t>(sample[0]) << 8) |
                         (static_cast<std::int32_t>(sample[1]) << 16) |
                         (static_cast<std::int32_t>(sample[2]) << 24);
    value >>= 8;
    return static_cast<float>(value) / 8388608.0F;
  }
  case ma_format_s32: {
    std::int32_t value = 0;
    std::memcpy(&value, sample, sizeof(value));
    return static_cast<float>(value) / 2147483648.0F;
  }
  case ma_format_f32: {
    float value = 0.0F;
    std::memcpy(&value, sample, sizeof(value));
    return value;
  }
  default:
    return 1.0F;
  }
}

[[nodiscard]] ma_uint64 detectLeadingSilenceFrames(ma_sound* sound) {
  ma_format format = ma_format_unknown;
  ma_uint32 channels = 0;
  ma_uint32 sampleRate = 0;
  if (ma_sound_get_data_format(sound, &format, &channels, &sampleRate, nullptr, 0) != MA_SUCCESS || channels == 0 || sampleRate == 0) {
    return 0;
  }

  const ma_uint32 bytesPerFrame = ma_get_bytes_per_frame(format, channels);
  if (bytesPerFrame == 0) {
    return 0;
  }

  constexpr float threshold = 0.0032F; // About -50 dBFS.
  const ma_uint64 maxScanFrames = sampleRate / 4;
  constexpr ma_uint64 framesPerRead = 256;
  std::vector<unsigned char> buffer(static_cast<std::size_t>(framesPerRead * bytesPerFrame));

  ma_uint64 scannedFrames = 0;
  while (scannedFrames < maxScanFrames) {
    const ma_uint64 framesToRead = std::min(framesPerRead, maxScanFrames - scannedFrames);
    ma_uint64 framesRead = 0;
    if (ma_data_source_read_pcm_frames(ma_sound_get_data_source(sound), buffer.data(), framesToRead, &framesRead) != MA_SUCCESS || framesRead == 0) {
      break;
    }

    for (ma_uint64 frame = 0; frame < framesRead; ++frame) {
      const unsigned char* frameData = buffer.data() + static_cast<std::size_t>(frame * bytesPerFrame);
      for (ma_uint32 channel = 0; channel < channels; ++channel) {
        const unsigned char* sample = frameData + (channel * ma_get_bytes_per_sample(format));
        if (std::abs(pcmSampleAsFloat(sample, format)) >= threshold) {
          (void)ma_sound_seek_to_pcm_frame(sound, 0);
          return scannedFrames + frame;
        }
      }
    }

    scannedFrames += framesRead;
  }

  (void)ma_sound_seek_to_pcm_frame(sound, 0);
  return 0;
}
#endif

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
    config.onProcess = &AudioSystem::audioProcessDebug;
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
  ma_engine_set_volume(static_cast<ma_engine*>(engine_), masterVolume_);
  initialized_ = true;
  if (audioDebugEnabled()) {
    auto* maEngine = static_cast<ma_engine*>(engine_);
    std::cerr << "Audio debug: miniaudio initialized at " << ma_engine_get_sample_rate(maEngine)
              << " Hz, " << ma_engine_get_channels(maEngine) << " channel(s), requested period " << config.periodSizeInFrames << " frames.\n";
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
      sounds_[asset.id].trimFrames = detectLeadingSilenceFrames(cachedSound.get());
      if (audioDebugEnabled() && sounds_[asset.id].trimFrames > 0) {
        ma_uint32 sampleRate = 0;
        (void)ma_sound_get_data_format(cachedSound.get(), nullptr, nullptr, &sampleRate, nullptr, 0);
        const double trimMs = sampleRate > 0 ? (static_cast<double>(sounds_[asset.id].trimFrames) * 1000.0 / static_cast<double>(sampleRate)) : 0.0;
        std::cerr << "Audio debug: will trim " << trimMs << " ms of leading silence from " << asset.id << ".\n";
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
  if (context_ != nullptr) {
    ma_context_uninit(static_cast<ma_context*>(context_));
    delete static_cast<ma_context*>(context_);
    context_ = nullptr;
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
