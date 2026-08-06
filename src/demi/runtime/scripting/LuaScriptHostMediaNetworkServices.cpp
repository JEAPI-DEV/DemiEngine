#include "demi/runtime/scene/components/EngineComponents.h"
#include "demi/runtime/scripting/LuaScriptHost.h"
#include "demi/runtime/scene/WorldQueries.h"

#include "demi/runtime/audio/AudioSystem.h"
#include "demi/runtime/media/MediaSystem.h"
#include "demi/runtime/network/NetworkSystem.h"

#include <utility>
#include <vector>

namespace demi::runtime {

std::uint64_t LuaScriptHost::playAudio(const std::string &assetId) {
  return audio_ != nullptr ? audio_->play(assetId) : 0;
}

std::uint64_t
LuaScriptHost::playAudio(const AudioPlaybackRequest &request) {
  return audio_ != nullptr ? audio_->play(request) : 0;
}

std::uint64_t LuaScriptHost::playAudioSource(const std::string &entityId) {
  if (world_ == nullptr || audio_ == nullptr) {
    return 0;
  }
  Entity *entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->hasComponent<AudioSourceComponent>() ||
      entity->component<AudioSourceComponent>()->clip.empty()) {
    return 0;
  }
  if (entity->component<AudioSourceComponent>()->handle != 0) {
    (void)audio_->stop(entity->component<AudioSourceComponent>()->handle);
  }
  auto *source = entity->component<AudioSourceComponent>();
  AudioVector3 position;
  if (entity->hasComponent<Transform3DComponent>()) {
    const Vec3 value = worldPosition3D(*world_, *entity);
    position = {value.x, value.y, value.z};
  } else if (const auto *transform =
                 entity->component<Transform2DComponent>()) {
    position = {transform->position.x, transform->position.y, 0.0F};
  }
  source->handle = audio_->play(
      {.assetId = source->clip,
       .bus = source->bus,
       .concurrencyGroup = source->concurrencyGroup,
       .loop = source->loop,
       .streaming = source->streaming,
       .volume = source->volume,
       .pitch = source->pitch,
       .pan = source->pan,
       .spatialMode = source->spatialMode,
       .attenuation = source->attenuation,
       .position = position,
       .velocity = {},
       .minDistance = source->minDistance,
       .maxDistance = source->maxDistance,
       .rolloff = source->rolloff,
       .doppler = source->doppler,
       .fadeInSeconds = source->fadeIn,
       .maxVoices = source->maxVoices,
       .voiceStealing = source->voiceStealing,
       .pauseWithGame = source->pauseWithGame});
  return source->handle;
}

bool LuaScriptHost::stopAudioSource(const std::string &entityId) {
  if (world_ == nullptr || audio_ == nullptr) {
    return false;
  }
  Entity *entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->hasComponent<AudioSourceComponent>() ||
      entity->component<AudioSourceComponent>()->handle == 0) {
    return false;
  }
  const bool stopped =
      audio_->stop(entity->component<AudioSourceComponent>()->handle);
  entity->component<AudioSourceComponent>()->handle = 0;
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

bool LuaScriptHost::setAudioBusVolume(const std::string &bus,
                                      const float volume) {
  return audio_ != nullptr && audio_->mixer().setVolume(bus, volume);
}

float LuaScriptHost::audioBusVolume(const std::string &bus) const {
  return audio_ != nullptr ? audio_->mixer().volume(bus) : 0.0F;
}

bool LuaScriptHost::setAudioBusMuted(const std::string &bus,
                                     const bool muted) {
  return audio_ != nullptr && audio_->mixer().setMuted(bus, muted);
}

bool LuaScriptHost::setAudioBusPaused(const std::string &bus,
                                      const bool paused) {
  return audio_ != nullptr && audio_->mixer().setPaused(bus, paused);
}

void LuaScriptHost::defineAudioSnapshot(
    const std::string &name,
    const std::unordered_map<std::string, float> &volumes) {
  if (audio_ != nullptr)
    audio_->mixer().defineSnapshot(name, {.volumes = volumes});
}

bool LuaScriptHost::transitionAudioSnapshot(const std::string &name,
                                            const float duration) {
  return audio_ != nullptr &&
         audio_->mixer().transitionTo(name, duration);
}

std::uint64_t LuaScriptHost::crossfadeAudio(
    const std::uint64_t fromHandle, const std::string &assetId,
    const std::string &bus, const float duration, const bool loop,
    const bool streaming) {
  if (audio_ == nullptr)
    return 0;
  if (fromHandle != 0)
    (void)audio_->stop(fromHandle, duration);
  return audio_->play({.assetId = assetId,
                       .bus = bus,
                       .concurrencyGroup = {},
                       .loop = loop,
                       .streaming = streaming,
                       .position = {},
                       .velocity = {},
                       .fadeInSeconds = duration});
}

std::uint64_t LuaScriptHost::playVideo(const std::string &assetId,
                                       const bool loop) {
  return media_ != nullptr ? media_->playVideo(assetId, loop) : 0;
}

std::uint64_t LuaScriptHost::playVideoPlayer(const std::string &entityId) {
  if (world_ == nullptr || media_ == nullptr) {
    return 0;
  }
  Entity *entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->hasComponent<VideoPlayerComponent>() ||
      entity->component<VideoPlayerComponent>()->clip.empty()) {
    return 0;
  }
  if (entity->component<VideoPlayerComponent>()->handle != 0) {
    (void)media_->stopVideo(entity->component<VideoPlayerComponent>()->handle);
  }
  entity->component<VideoPlayerComponent>()->handle =
      media_->playVideo(entity->component<VideoPlayerComponent>()->clip,
                        entity->component<VideoPlayerComponent>()->loop);
  return entity->component<VideoPlayerComponent>()->handle;
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

bool LuaScriptHost::networkHost(const std::uint16_t port,
                                const std::uint32_t maxPeers) {
  return network_ != nullptr && network_->host(port, maxPeers);
}

bool LuaScriptHost::networkHostSecure(const std::uint16_t port,
                                      const std::string &certificate,
                                      const std::string &privateKey,
                                      const std::uint32_t maxPeers) {
  const std::filesystem::path certificateValue(certificate);
  const std::filesystem::path privateKeyValue(privateKey);
  const std::filesystem::path certificatePath =
      certificateValue.is_absolute() ? certificateValue
                                     : projectDirectory_ / certificateValue;
  const std::filesystem::path privateKeyPath =
      privateKeyValue.is_absolute() ? privateKeyValue
                                    : projectDirectory_ / privateKeyValue;
  return network_ != nullptr &&
         network_->hostSecure(port, certificatePath, privateKeyPath, maxPeers);
}

bool LuaScriptHost::networkConnect(const std::string &address,
                                   const std::uint16_t port) {
  return network_ != nullptr && network_->connect(address, port);
}

bool LuaScriptHost::networkConnectSecure(const std::string &address,
                                         const std::uint16_t port,
                                         const std::string &trustedCertificate,
                                         const std::string &serverName) {
  const std::filesystem::path certificateValue(trustedCertificate);
  const std::filesystem::path certificatePath =
      certificateValue.is_absolute() ? certificateValue
                                     : projectDirectory_ / certificateValue;
  return network_ != nullptr &&
         network_->connectSecure(address, port, certificatePath, serverName);
}

void LuaScriptHost::networkDisconnect() {
  if (network_ != nullptr) {
    network_->disconnect();
  }
}

void LuaScriptHost::networkDisconnectPeer(const std::uint32_t peerId) {
  if (network_ != nullptr)
    network_->disconnectPeer(peerId);
}

bool LuaScriptHost::networkSend(const std::string &message, const bool reliable,
                                const std::uint8_t channel,
                                const std::uint32_t peerId) {
  return network_ != nullptr &&
         network_->send(message, reliable, channel, peerId);
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

bool LuaScriptHost::networkIsSecure() const {
  return network_ != nullptr && network_->isSecure();
}

std::string LuaScriptHost::networkSecurityError() const {
  return network_ != nullptr ? network_->securityError()
                             : "Networking is unavailable.";
}

std::vector<NetworkEvent> LuaScriptHost::networkDrainEvents() {
  return network_ != nullptr ? network_->drainEvents()
                             : std::vector<NetworkEvent>{};
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

const std::string &LuaScriptHost::activeCutscene() const {
  return activeCutscene_;
}

} // namespace demi::runtime
