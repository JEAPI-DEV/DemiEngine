#pragma once

#include <cstdint>
#include <string>

namespace demi::runtime {

struct AudioVector3 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

enum class AudioSpatialMode {
  None,
  TwoDimensional,
  ThreeDimensional,
};

enum class AudioAttenuation {
  None,
  Inverse,
  Linear,
  Exponential,
};

enum class AudioVoiceStealing {
  Reject,
  Oldest,
  Quietest,
};

struct AudioPlaybackRequest {
  std::string assetId;
  std::string bus = "sfx";
  std::string concurrencyGroup;
  bool loop = false;
  bool streaming = false;
  float volume = 1.0F;
  float pitch = 1.0F;
  float pan = 0.0F;
  AudioSpatialMode spatialMode = AudioSpatialMode::None;
  AudioAttenuation attenuation = AudioAttenuation::Inverse;
  AudioVector3 position;
  AudioVector3 velocity;
  float minDistance = 1.0F;
  float maxDistance = 100.0F;
  float rolloff = 1.0F;
  bool doppler = false;
  float delaySeconds = 0.0F;
  float fadeInSeconds = 0.0F;
  std::uint32_t maxVoices = 0;
  AudioVoiceStealing voiceStealing = AudioVoiceStealing::Oldest;
  bool pauseWithGame = true;
};

struct AudioBackendVoiceParameters {
  float gain = 1.0F;
  float pitch = 1.0F;
  float pan = 0.0F;
  AudioSpatialMode spatialMode = AudioSpatialMode::None;
  AudioAttenuation attenuation = AudioAttenuation::Inverse;
  AudioVector3 position;
  AudioVector3 velocity;
  float minDistance = 1.0F;
  float maxDistance = 100.0F;
  float rolloff = 1.0F;
  bool doppler = false;
  bool paused = false;
};

struct AudioListenerState {
  AudioVector3 position;
  AudioVector3 forward = {0.0F, 0.0F, -1.0F};
  AudioVector3 up = {0.0F, 1.0F, 0.0F};
  AudioVector3 velocity;
};

} // namespace demi::runtime
