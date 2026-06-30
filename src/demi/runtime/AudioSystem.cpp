#include "demi/runtime/AudioSystem.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#if DEMI_HAS_SDL3
#include <SDL3/SDL.h>
#endif

#if DEMI_HAS_MPG123
#include <mpg123.h>
#endif

namespace demi::runtime {

namespace {

#if DEMI_HAS_SDL3

void audioStreamCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount) {
  (void)totalAmount;
  static_cast<AudioSystem*>(userdata)->mixAudio(stream, additionalAmount);
}

#endif

#if DEMI_HAS_MPG123

std::vector<unsigned char> decodeMp3(const std::filesystem::path& path, long& rate, int& channels) {
  std::vector<unsigned char> pcm;

  int error = MPG123_OK;
  mpg123_handle* handle = mpg123_new(nullptr, &error);
  if (handle == nullptr) {
    std::cerr << "mpg123_new failed for " << path.string() << '\n';
    return pcm;
  }

  mpg123_format_none(handle);
  mpg123_format(handle, 44100, MPG123_STEREO, MPG123_ENC_SIGNED_16);

  if (mpg123_open(handle, path.string().c_str()) != MPG123_OK) {
    std::cerr << "mpg123_open failed for " << path.string() << ": " << mpg123_strerror(handle) << '\n';
    mpg123_delete(handle);
    return pcm;
  }

  int encoding = 0;
  if (mpg123_getformat(handle, &rate, &channels, &encoding) != MPG123_OK) {
    std::cerr << "mpg123_getformat failed for " << path.string() << ": " << mpg123_strerror(handle) << '\n';
    mpg123_close(handle);
    mpg123_delete(handle);
    return pcm;
  }

  constexpr std::size_t ChunkSize = 8192;
  std::vector<unsigned char> chunk(ChunkSize);
  while (true) {
    std::size_t bytesRead = 0;
    const int result = mpg123_read(handle, chunk.data(), chunk.size(), &bytesRead);
    if (bytesRead > 0) {
      pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + static_cast<std::ptrdiff_t>(bytesRead));
    }
    if (result == MPG123_DONE) {
      break;
    }
    if (result != MPG123_OK) {
      std::cerr << "mpg123_read failed for " << path.string() << ": " << mpg123_strerror(handle) << '\n';
      pcm.clear();
      break;
    }
  }

  mpg123_close(handle);
  mpg123_delete(handle);
  return pcm;
}

#endif

} // namespace

AudioSystem::AudioSystem() = default;

AudioSystem::~AudioSystem() {
  shutdown();
}

void AudioSystem::shutdown() {
#if DEMI_HAS_SDL3
  if (outputStream_ != nullptr) {
    SDL_DestroyAudioStream(static_cast<SDL_AudioStream*>(outputStream_));
    outputStream_ = nullptr;
  }
  if (initialized_) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
#endif
#if DEMI_HAS_MPG123
  if (initialized_) {
    mpg123_exit();
  }
#endif
  initialized_ = false;
}

bool AudioSystem::initialize() {
#if !DEMI_HAS_SDL3 || !DEMI_HAS_MPG123
  return false;
#else
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    std::cerr << "SDL audio init failed; audio disabled: " << SDL_GetError() << '\n';
    return false;
  }

  if (mpg123_init() != MPG123_OK) {
    std::cerr << "mpg123_init failed; audio disabled.\n";
    return false;
  }

  SDL_AudioSpec spec{
    .format = SDL_AUDIO_S16,
    .channels = 2,
    .freq = 44100,
  };
  outputStream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audioStreamCallback, this);
  if (outputStream_ == nullptr) {
    std::cerr << "SDL_OpenAudioDeviceStream failed; audio disabled: " << SDL_GetError() << '\n';
    mpg123_exit();
    return false;
  }

  SDL_ResumeAudioStreamDevice(static_cast<SDL_AudioStream*>(outputStream_));
  initialized_ = true;
  return true;
#endif
}

void AudioSystem::loadAudioAssets(const AssetRegistry& registry) {
#if DEMI_HAS_MPG123
  if (!initialized_) {
    return;
  }

  for (const AssetManifest& asset : registry.assets) {
    if (asset.type != "AudioClip") {
      continue;
    }

    long rate = 44100;
    int channels = 2;
    std::vector<unsigned char> pcm = decodeMp3(asset.sourcePath, rate, channels);
    if (pcm.empty()) {
      std::cerr << "Audio load failed for " << asset.id << " from " << asset.sourcePath.string() << '\n';
      continue;
    }

    sounds_[asset.id] = SoundData{.frequency = static_cast<int>(rate), .channels = channels, .pcm = std::move(pcm)};
  }
#else
  (void)registry;
#endif
}

std::uint64_t AudioSystem::play(const std::string& assetId) {
#if !DEMI_HAS_SDL3
  (void)assetId;
  return 0;
#else
  const std::uint64_t requestTime = SDL_GetPerformanceCounter();
  const auto sound = sounds_.find(assetId);
  if (sound == sounds_.end()) {
    std::cerr << "Audio asset not loaded: " << assetId << '\n';
    return 0;
  }

  if (outputStream_ == nullptr || !initialized_) {
    std::cerr << "Audio output stream is not initialized for " << assetId << '\n';
    return 0;
  }

  std::uint64_t handle = 0;
  {
    std::scoped_lock lock(mutex_);
    handle = nextHandle_++;
    playing_.push_back(PlayingSound{.handle = handle, .sound = &sound->second, .cursor = 0, .requestedAt = requestTime});
  }

  std::cerr << "Audio.play queued " << assetId << " handle " << handle << '\n';
  return handle;
#endif
}

bool AudioSystem::stop(const std::uint64_t handle) {
  if (handle == 0) {
    return false;
  }

  std::scoped_lock lock(mutex_);
  const auto before = playing_.size();
  std::erase_if(playing_, [&](const PlayingSound& playing) { return playing.handle == handle; });
  return playing_.size() != before;
}

void AudioSystem::setMasterVolume(const float volume) {
  std::scoped_lock lock(mutex_);
  masterVolume_ = std::clamp(volume, 0.0F, 1.0F);
}

float AudioSystem::masterVolume() const {
  return masterVolume_;
}

void AudioSystem::update() {
}

void AudioSystem::mixAudio(void* stream, const int additionalAmount) {
#if DEMI_HAS_SDL3
  if (additionalAmount <= 0) {
    return;
  }

  const int bytes = additionalAmount - (additionalAmount % static_cast<int>(sizeof(std::int16_t)));
  if (bytes <= 0) {
    return;
  }

  std::vector<std::int16_t> mix(static_cast<std::size_t>(bytes / static_cast<int>(sizeof(std::int16_t))), 0);

  {
    std::scoped_lock lock(mutex_);
    for (PlayingSound& playing : playing_) {
      if (playing.sound == nullptr || playing.cursor >= playing.sound->pcm.size()) {
        continue;
      }

      if (!playing.firstMixLogged) {
        const double elapsedMs = (static_cast<double>(SDL_GetPerformanceCounter() - playing.requestedAt) * 1000.0) /
                                 static_cast<double>(SDL_GetPerformanceFrequency());
        std::cerr << "Audio first mix after " << elapsedMs << " ms\n";
        playing.firstMixLogged = true;
      }

      const std::size_t remainingBytes = playing.sound->pcm.size() - playing.cursor;
      const std::size_t bytesToMix = std::min<std::size_t>(remainingBytes, mix.size() * sizeof(std::int16_t));
      const auto* source = reinterpret_cast<const std::int16_t*>(playing.sound->pcm.data() + playing.cursor);
      const std::size_t samplesToMix = bytesToMix / sizeof(std::int16_t);
      const float volume = masterVolume_;

      for (std::size_t i = 0; i < samplesToMix; ++i) {
        const int value = static_cast<int>(mix[i]) + static_cast<int>(static_cast<float>(source[i]) * volume);
        mix[i] = static_cast<std::int16_t>(std::clamp(value, -32768, 32767));
      }

      playing.cursor += samplesToMix * sizeof(std::int16_t);
    }

    std::erase_if(playing_, [](const PlayingSound& playing) {
      return playing.sound == nullptr || playing.cursor >= playing.sound->pcm.size();
    });
  }

  SDL_PutAudioStreamData(static_cast<SDL_AudioStream*>(stream), mix.data(), bytes);
#else
  (void)stream;
  (void)additionalAmount;
#endif
}

} // namespace demi::runtime
