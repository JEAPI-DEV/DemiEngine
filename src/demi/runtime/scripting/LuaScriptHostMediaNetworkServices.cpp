#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/audio/AudioSystem.h"
#include "demi/runtime/media/MediaSystem.h"
#include "demi/runtime/network/NetworkSystem.h"

#include <utility>
#include <vector>

namespace demi::runtime {

std::uint64_t LuaScriptHost::playAudio(const std::string& assetId) {
  return audio_ != nullptr ? audio_->play(assetId) : 0;
}

std::uint64_t LuaScriptHost::playAudioSource(const std::string& entityId) {
  if (world_ == nullptr || audio_ == nullptr) {
    return 0;
  }
  Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->audioSource.has_value() || entity->audioSource->clip.empty()) {
    return 0;
  }
  if (entity->audioSource->handle != 0) {
    (void)audio_->stop(entity->audioSource->handle);
  }
  entity->audioSource->handle = audio_->play(entity->audioSource->clip, entity->audioSource->loop, entity->audioSource->volume);
  return entity->audioSource->handle;
}

bool LuaScriptHost::stopAudioSource(const std::string& entityId) {
  if (world_ == nullptr || audio_ == nullptr) {
    return false;
  }
  Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->audioSource.has_value() || entity->audioSource->handle == 0) {
    return false;
  }
  const bool stopped = audio_->stop(entity->audioSource->handle);
  entity->audioSource->handle = 0;
  return stopped;
}

bool LuaScriptHost::stopAudio(const std::uint64_t handle) {
  return audio_ != nullptr && audio_->stop(handle);
}

void LuaScriptHost::setMasterVolume(const float volume) {
  if (audio_ != nullptr) {
    audio_->setMasterVolume(volume);
  }
}

float LuaScriptHost::masterVolume() const {
  return audio_ != nullptr ? audio_->masterVolume() : 1.0F;
}

std::uint64_t LuaScriptHost::playVideo(const std::string& assetId, const bool loop) {
  return media_ != nullptr ? media_->playVideo(assetId, loop) : 0;
}

std::uint64_t LuaScriptHost::playVideoPlayer(const std::string& entityId) {
  if (world_ == nullptr || media_ == nullptr) {
    return 0;
  }
  Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->videoPlayer.has_value() || entity->videoPlayer->clip.empty()) {
    return 0;
  }
  if (entity->videoPlayer->handle != 0) {
    (void)media_->stopVideo(entity->videoPlayer->handle);
  }
  entity->videoPlayer->handle = media_->playVideo(entity->videoPlayer->clip, entity->videoPlayer->loop);
  return entity->videoPlayer->handle;
}

bool LuaScriptHost::stopVideo(const std::uint64_t handle) {
  return media_ != nullptr && media_->stopVideo(handle);
}

bool LuaScriptHost::isVideoPlaying(const std::uint64_t handle) const {
  return media_ != nullptr && media_->isVideoPlaying(handle);
}

bool LuaScriptHost::networkAvailable() const {
  return network_ != nullptr && network_->available();
}

bool LuaScriptHost::networkHost(const std::uint16_t port, const std::uint32_t maxPeers) {
  return network_ != nullptr && network_->host(port, maxPeers);
}

bool LuaScriptHost::networkConnect(const std::string& address, const std::uint16_t port) {
  return network_ != nullptr && network_->connect(address, port);
}

void LuaScriptHost::networkDisconnect() {
  if (network_ != nullptr) {
    network_->disconnect();
  }
}

bool LuaScriptHost::networkSend(const std::string& message, const bool reliable, const std::uint8_t channel, const std::uint32_t peerId) {
  return network_ != nullptr && network_->send(message, reliable, channel, peerId);
}

bool LuaScriptHost::networkIsHost() const {
  return network_ != nullptr && network_->isHosting();
}

bool LuaScriptHost::networkIsConnected() const {
  return network_ != nullptr && network_->isConnected();
}

std::uint32_t LuaScriptHost::networkLatencyMs() const {
  return network_ != nullptr ? network_->latencyMs() : 0;
}

std::vector<NetworkEvent> LuaScriptHost::networkDrainEvents() {
  return network_ != nullptr ? network_->drainEvents() : std::vector<NetworkEvent>{};
}

bool LuaScriptHost::startCutscene(std::string id) {
  if (id.empty()) {
    return false;
  }
  activeCutscene_ = std::move(id);
  cutscenePaused_ = false;
  return true;
}

bool LuaScriptHost::pauseCutscene() {
  if (activeCutscene_.empty() || cutscenePaused_) {
    return false;
  }
  cutscenePaused_ = true;
  return true;
}

bool LuaScriptHost::resumeCutscene() {
  if (activeCutscene_.empty() || !cutscenePaused_) {
    return false;
  }
  cutscenePaused_ = false;
  return true;
}

bool LuaScriptHost::stopCutscene() {
  if (activeCutscene_.empty()) {
    return false;
  }
  activeCutscene_.clear();
  cutscenePaused_ = false;
  return true;
}

bool LuaScriptHost::isCutscenePlaying() const {
  return !activeCutscene_.empty() && !cutscenePaused_;
}

const std::string& LuaScriptHost::activeCutscene() const {
  return activeCutscene_;
}

} // namespace demi::runtime
