#include "demi/runtime/audio/AudioClipPreprocess.h"

#if DEMI_HAS_MINIAUDIO
#include <miniaudio.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>
#endif

namespace demi::runtime {

#if DEMI_HAS_MINIAUDIO
namespace {

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

} // namespace

std::uint64_t detectLeadingSilenceFrames(ma_sound* sound) {
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

  constexpr float threshold = 0.0032F;
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

} // namespace demi::runtime
