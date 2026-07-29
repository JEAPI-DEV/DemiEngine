#pragma once

#include <string>
#include <unordered_map>

namespace demi::runtime {

struct AudioBusState {
  std::string parent = "master";
  float volume = 1.0F;
  bool muted = false;
  bool paused = false;
};

struct AudioMixerSnapshot {
  std::unordered_map<std::string, float> volumes;
};

class AudioMixer {
public:
  AudioMixer();

  [[nodiscard]] bool defineBus(const std::string &name,
                               const std::string &parent = "master");
  [[nodiscard]] bool setVolume(const std::string &name, float volume);
  [[nodiscard]] float volume(const std::string &name) const;
  [[nodiscard]] bool setMuted(const std::string &name, bool muted);
  [[nodiscard]] bool muted(const std::string &name) const;
  [[nodiscard]] bool setPaused(const std::string &name, bool paused);
  [[nodiscard]] bool paused(const std::string &name) const;
  [[nodiscard]] float effectiveGain(const std::string &name) const;
  [[nodiscard]] bool effectivelyPaused(const std::string &name) const;

  void defineSnapshot(std::string name, AudioMixerSnapshot snapshot);
  [[nodiscard]] bool transitionTo(const std::string &name,
                                  float durationSeconds);
  void update(float deltaTime);

private:
  struct SnapshotTransition {
    std::unordered_map<std::string, float> from;
    std::unordered_map<std::string, float> to;
    float duration = 0.0F;
    float elapsed = 0.0F;
    bool active = false;
  };

  [[nodiscard]] bool wouldCreateCycle(const std::string &name,
                                      const std::string &parent) const;

  std::unordered_map<std::string, AudioBusState> buses_;
  std::unordered_map<std::string, AudioMixerSnapshot> snapshots_;
  SnapshotTransition transition_;
};

} // namespace demi::runtime
