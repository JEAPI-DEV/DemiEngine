#include "demi/runtime/audio/AudioMixer.h"

#include <algorithm>
#include <unordered_set>

namespace demi::runtime {

AudioMixer::AudioMixer() {
  buses_.emplace("master", AudioBusState{.parent = {}});
  (void)defineBus("music");
  (void)defineBus("sfx");
  (void)defineBus("voice");
  (void)defineBus("ui");
}

bool AudioMixer::wouldCreateCycle(const std::string &name,
                                  const std::string &parent) const {
  std::unordered_set<std::string> visited{name};
  std::string cursor = parent;
  while (!cursor.empty()) {
    if (!visited.insert(cursor).second)
      return true;
    const auto found = buses_.find(cursor);
    if (found == buses_.end())
      return false;
    cursor = found->second.parent;
  }
  return false;
}

bool AudioMixer::defineBus(const std::string &name, const std::string &parent) {
  if (name.empty() || name == "master" || !buses_.contains(parent) ||
      wouldCreateCycle(name, parent))
    return false;
  buses_[name].parent = parent;
  return true;
}

bool AudioMixer::setVolume(const std::string &name, const float volume) {
  const auto found = buses_.find(name);
  if (found == buses_.end())
    return false;
  found->second.volume = std::max(volume, 0.0F);
  return true;
}

float AudioMixer::volume(const std::string &name) const {
  const auto found = buses_.find(name);
  return found == buses_.end() ? 0.0F : found->second.volume;
}

bool AudioMixer::setMuted(const std::string &name, const bool mutedValue) {
  const auto found = buses_.find(name);
  if (found == buses_.end())
    return false;
  found->second.muted = mutedValue;
  return true;
}

bool AudioMixer::muted(const std::string &name) const {
  const auto found = buses_.find(name);
  return found != buses_.end() && found->second.muted;
}

bool AudioMixer::setPaused(const std::string &name, const bool pausedValue) {
  const auto found = buses_.find(name);
  if (found == buses_.end())
    return false;
  found->second.paused = pausedValue;
  return true;
}

bool AudioMixer::paused(const std::string &name) const {
  const auto found = buses_.find(name);
  return found != buses_.end() && found->second.paused;
}

float AudioMixer::effectiveGain(const std::string &name) const {
  float result = 1.0F;
  std::unordered_set<std::string> visited;
  std::string cursor = buses_.contains(name) ? name : "master";
  while (!cursor.empty() && visited.insert(cursor).second) {
    const auto found = buses_.find(cursor);
    if (found == buses_.end())
      break;
    if (found->second.muted)
      return 0.0F;
    result *= found->second.volume;
    cursor = found->second.parent;
  }
  return result;
}

bool AudioMixer::effectivelyPaused(const std::string &name) const {
  std::unordered_set<std::string> visited;
  std::string cursor = buses_.contains(name) ? name : "master";
  while (!cursor.empty() && visited.insert(cursor).second) {
    const auto found = buses_.find(cursor);
    if (found == buses_.end())
      break;
    if (found->second.paused)
      return true;
    cursor = found->second.parent;
  }
  return false;
}

void AudioMixer::defineSnapshot(std::string name, AudioMixerSnapshot snapshot) {
  snapshots_[std::move(name)] = std::move(snapshot);
}

bool AudioMixer::transitionTo(const std::string &name,
                              const float durationSeconds) {
  const auto found = snapshots_.find(name);
  if (found == snapshots_.end())
    return false;
  transition_ = {};
  transition_.duration = std::max(durationSeconds, 0.0F);
  for (const auto &[bus, target] : found->second.volumes) {
    if (!buses_.contains(bus))
      continue;
    transition_.from[bus] = buses_.at(bus).volume;
    transition_.to[bus] = std::max(target, 0.0F);
  }
  transition_.active = !transition_.to.empty();
  if (transition_.duration == 0.0F)
    update(0.0F);
  return true;
}

void AudioMixer::update(const float deltaTime) {
  if (!transition_.active)
    return;
  transition_.elapsed += std::max(deltaTime, 0.0F);
  const float amount =
      transition_.duration <= 0.0F
          ? 1.0F
          : std::clamp(transition_.elapsed / transition_.duration, 0.0F, 1.0F);
  for (const auto &[bus, target] : transition_.to) {
    const float start = transition_.from.at(bus);
    buses_[bus].volume = start + (target - start) * amount;
  }
  if (amount >= 1.0F)
    transition_.active = false;
}

} // namespace demi::runtime
