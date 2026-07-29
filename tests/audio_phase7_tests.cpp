#include "demi/runtime/audio/AudioBackend.h"
#include "demi/runtime/audio/AudioMixer.h"
#include "demi/runtime/audio/AudioSystem.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <unordered_map>

using namespace demi::runtime;

namespace {

class RecordingBackend final : public AudioBackend {
public:
  bool initialize() override {
    initialized = true;
    return true;
  }
  void loadAudioAssets(const demi::AssetRegistry &) override {}
  std::uint64_t play(const std::string &assetId, bool, bool streaming) override {
    const std::uint64_t handle = next++;
    assets[handle] = assetId;
    streamed[handle] = streaming;
    playing[handle] = true;
    ++playCount;
    return handle;
  }
  bool stop(std::uint64_t handle) override {
    if (!playing.contains(handle))
      return false;
    playing.erase(handle);
    ++stopCount;
    return true;
  }
  bool configure(
      std::uint64_t handle,
      const AudioBackendVoiceParameters &parameters) override {
    if (!playing.contains(handle))
      return false;
    configured[handle] = parameters;
    return true;
  }
  bool isPlaying(std::uint64_t handle) const override {
    return playing.contains(handle) && playing.at(handle);
  }
  void setListener(const AudioListenerState &value) override {
    listener = value;
  }
  void setDeviceSuspended(bool value) override { suspended = value; }
  void update() override {}
  void shutdown() override { playing.clear(); }

  bool initialized = false;
  bool suspended = false;
  int playCount = 0;
  int stopCount = 0;
  std::uint64_t next = 1;
  AudioListenerState listener;
  std::unordered_map<std::uint64_t, std::string> assets;
  std::unordered_map<std::uint64_t, bool> streamed;
  std::unordered_map<std::uint64_t, bool> playing;
  std::unordered_map<std::uint64_t, AudioBackendVoiceParameters> configured;
};

bool near(float left, float right) {
  return std::abs(left - right) < 0.001F;
}

} // namespace

int main() {
  AudioMixer mixer;
  if (!mixer.defineBus("weapons", "sfx") ||
      mixer.defineBus("bad", "missing") ||
      !mixer.setVolume("master", 0.5F) ||
      !mixer.setVolume("sfx", 0.8F) ||
      !mixer.setVolume("weapons", 0.5F) ||
      !near(mixer.effectiveGain("weapons"), 0.2F)) {
    std::cerr << "Audio bus routing failed.\n";
    return 1;
  }
  (void)mixer.setMuted("sfx", true);
  if (mixer.effectiveGain("weapons") != 0.0F) {
    std::cerr << "Parent bus mute did not propagate.\n";
    return 1;
  }
  (void)mixer.setMuted("sfx", false);
  mixer.defineSnapshot("ducked", {.volumes = {{"music", 0.2F},
                                                {"sfx", 1.0F}}});
  if (!mixer.transitionTo("ducked", 2.0F)) {
    std::cerr << "Mixer snapshot was rejected.\n";
    return 1;
  }
  mixer.update(1.0F);
  if (!near(mixer.volume("music"), 0.6F)) {
    std::cerr << "Mixer snapshot interpolation failed.\n";
    return 1;
  }
  mixer.update(5.0F);
  if (!near(mixer.volume("music"), 0.2F)) {
    std::cerr << "Mixer snapshot did not clamp at its target.\n";
    return 1;
  }

  auto backend = std::make_unique<RecordingBackend>();
  RecordingBackend *recording = backend.get();
  AudioSystem audio(std::move(backend));
  if (!audio.initialize()) {
    std::cerr << "Injected audio backend did not initialize.\n";
    return 1;
  }
  const std::uint64_t scheduled =
      audio.play({.assetId = "asset://scheduled",
                  .bus = "music",
                  .streaming = true,
                  .volume = 0.8F,
                  .delaySeconds = 0.5F,
                  .fadeInSeconds = 1.0F});
  if (scheduled == 0 || recording->playCount != 0) {
    std::cerr << "Scheduled playback started early.\n";
    return 1;
  }
  audio.update(0.25F);
  if (recording->playCount != 0) {
    std::cerr << "Scheduled playback ignored its delay.\n";
    return 1;
  }
  audio.update(0.25F);
  if (recording->playCount != 1 || !recording->streamed.at(1)) {
    std::cerr << "Scheduled streaming voice did not start.\n";
    return 1;
  }
  audio.update(0.5F);
  if (!near(recording->configured.at(1).gain, 0.4F)) {
    std::cerr << "Fade-in gain was not evaluated by the engine layer.\n";
    return 1;
  }

  AudioPlaybackRequest limited{.assetId = "asset://shot",
                               .concurrencyGroup = "shots",
                               .maxVoices = 2,
                               .voiceStealing = AudioVoiceStealing::Oldest};
  const auto first = audio.play(limited);
  const auto second = audio.play(limited);
  const auto third = audio.play(limited);
  if (first == 0 || second == 0 || third == 0 || recording->stopCount != 1 ||
      audio.isPlaying(first)) {
    std::cerr << "Oldest voice stealing failed.\n";
    return 1;
  }
  limited.voiceStealing = AudioVoiceStealing::Reject;
  if (audio.play(limited) != 0) {
    std::cerr << "Concurrency rejection failed.\n";
    return 1;
  }

  audio.setGamePaused(true);
  audio.update(0.0F);
  if (!recording->configured.at(recording->next - 1).paused ||
      !audio.isPlaying(third)) {
    std::cerr << "Game pause policy did not reach the backend.\n";
    return 1;
  }
  audio.setSuspended(true);
  if (!recording->suspended) {
    std::cerr << "Device suspension did not reach the backend.\n";
    return 1;
  }
  audio.setListener({.position = {1.0F, 2.0F, 3.0F}});
  if (!near(recording->listener.position.z, 3.0F)) {
    std::cerr << "Listener state did not reach the backend.\n";
    return 1;
  }
  if (!audio.stop(scheduled, 0.25F)) {
    std::cerr << "Fade-out was rejected.\n";
    return 1;
  }
  audio.setGamePaused(false);
  audio.setSuspended(false);
  audio.update(0.25F);
  if (audio.isPlaying(scheduled)) {
    std::cerr << "Faded voice was not released.\n";
    return 1;
  }
  return 0;
}
