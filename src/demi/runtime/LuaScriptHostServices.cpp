#include "demi/runtime/LuaScriptHost.h"

#include "demi/runtime/AudioSystem.h"
#include "demi/runtime/LuaScriptHostInternal.h"
#include "demi/runtime/MediaSystem.h"
#include "demi/runtime/NetworkSystem.h"
#include "demi/runtime/Physics2D.h"
#include "demi/runtime/Physics3D.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <optional>

namespace demi::runtime {

std::string normalizedKey(std::string key) {
  std::ranges::transform(key, key.begin(), [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return key;
}

bool LuaScriptHost::isKeyDown(const std::string& key) const {
  return input_ != nullptr && input_->keysDown.contains(normalizedKey(key));
}

bool LuaScriptHost::addEntityPosition(const std::string& entityId, const float dx, const float dy) {
  if (world_ == nullptr) {
    return false;
  }
  Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->transform2D.has_value()) {
    return false;
  }
  entity->transform2D->position.x += dx;
  entity->transform2D->position.y += dy;
  return true;
}

bool LuaScriptHost::setEntityPosition(const std::string& entityId, const float x, const float y) {
  if (world_ == nullptr) {
    return false;
  }
  Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->transform2D.has_value()) {
    return false;
  }
  entity->transform2D->position = Vec2{.x = x, .y = y};
  return true;
}

std::optional<Vec2> LuaScriptHost::entityPosition(const std::string& entityId) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  const Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->transform2D.has_value()) {
    return std::nullopt;
  }
  return entity->transform2D->position;
}

std::optional<float> LuaScriptHost::entityRotation(const std::string& entityId) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  const Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->transform2D.has_value()) {
    return std::nullopt;
  }
  return entity->transform2D->rotation;
}

bool LuaScriptHost::setEntityRotation(const std::string& entityId, const float rotation) {
  if (world_ == nullptr) {
    return false;
  }
  Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->transform2D.has_value()) {
    return false;
  }
  entity->transform2D->rotation = rotation;
  return true;
}

std::optional<Vec2> LuaScriptHost::entityScale(const std::string& entityId) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  const Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->transform2D.has_value()) {
    return std::nullopt;
  }
  return entity->transform2D->scale;
}

bool LuaScriptHost::setEntityScale(const std::string& entityId, const float x, const float y) {
  if (world_ == nullptr) {
    return false;
  }
  Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->transform2D.has_value()) {
    return false;
  }
  entity->transform2D->scale = Vec2{.x = x, .y = y};
  return true;
}

bool LuaScriptHost::addEntityPosition3D(const std::string& entityId, const float dx, const float dy, const float dz) {
  if (world_ == nullptr) {
    return false;
  }
  Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->transform3D.has_value()) {
    return false;
  }
  entity->transform3D->position = resolveDynamicMove3D(*world_, *entity, entity->transform3D->position, Vec3{.x = dx, .y = dy, .z = dz});
  return true;
}

bool LuaScriptHost::setEntityPosition3D(const std::string& entityId, const float x, const float y, const float z) {
  if (world_ == nullptr) {
    return false;
  }
  Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->transform3D.has_value()) {
    return false;
  }
  const Vec3 target{.x = x, .y = y, .z = z};
  const Vec3 delta{.x = target.x - entity->transform3D->position.x, .y = target.y - entity->transform3D->position.y, .z = target.z - entity->transform3D->position.z};
  entity->transform3D->position = resolveDynamicMove3D(*world_, *entity, entity->transform3D->position, delta);
  return true;
}

std::optional<Vec3> LuaScriptHost::entityPosition3D(const std::string& entityId) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  const Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->transform3D.has_value()) {
    return std::nullopt;
  }
  return entity->transform3D->position;
}

std::optional<Vec3> LuaScriptHost::entityRotation3D(const std::string& entityId) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  const Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->transform3D.has_value()) {
    return std::nullopt;
  }
  return entity->transform3D->rotation;
}

bool LuaScriptHost::setEntityRotation3D(const std::string& entityId, const float x, const float y, const float z) {
  if (world_ == nullptr) {
    return false;
  }
  Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->transform3D.has_value()) {
    return false;
  }
  entity->transform3D->rotation = Vec3{.x = x, .y = y, .z = z};
  return true;
}

std::optional<Vec3> LuaScriptHost::entityScale3D(const std::string& entityId) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  const Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->transform3D.has_value()) {
    return std::nullopt;
  }
  return entity->transform3D->scale;
}

bool LuaScriptHost::setEntityScale3D(const std::string& entityId, const float x, const float y, const float z) {
  if (world_ == nullptr) {
    return false;
  }
  Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->transform3D.has_value()) {
    return false;
  }
  entity->transform3D->scale = Vec3{.x = x, .y = y, .z = z};
  return true;
}

std::optional<std::string> LuaScriptHost::findEntityId(const std::string& idOrName) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  for (const Entity& entity : world_->entities) {
    if (entity.id == idOrName || entity.name == idOrName) {
      return entity.id;
    }
  }
  return std::nullopt;
}

bool LuaScriptHost::destroyEntity(const std::string& entityId) {
  if (world_ == nullptr) {
    return false;
  }
  const auto before = world_->entities.size();
  std::erase_if(world_->entities, [&](const Entity& entity) { return entity.id == entityId; });
  return world_->entities.size() != before;
}

bool LuaScriptHost::setEntitySpriteColor(const std::string& entityId, const Color color) {
  if (world_ == nullptr) {
    return false;
  }
  Entity* entity = findEntity(*world_, entityId);
  if (entity == nullptr || !entity->sprite.has_value()) {
    return false;
  }
  entity->sprite->color = color;
  return true;
}

std::optional<Vec2> LuaScriptHost::getRigidbodyVelocity(const std::string& entityId) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  return rigidbodyVelocity(*world_, entityId);
}

bool LuaScriptHost::setRigidbodyVelocity(const std::string& entityId, const float x, const float y) {
  return world_ != nullptr && demi::runtime::setRigidbodyVelocity(*world_, entityId, Vec2{.x = x, .y = y});
}

bool LuaScriptHost::setRigidbodyVelocityX(const std::string& entityId, const float x) {
  return world_ != nullptr && demi::runtime::setRigidbodyVelocityX(*world_, entityId, x);
}

bool LuaScriptHost::setRigidbodyVelocityY(const std::string& entityId, const float y) {
  return world_ != nullptr && demi::runtime::setRigidbodyVelocityY(*world_, entityId, y);
}

bool LuaScriptHost::addRigidbodyImpulse(const std::string& entityId, const float x, const float y) {
  return world_ != nullptr && demi::runtime::addRigidbodyImpulse(*world_, entityId, Vec2{.x = x, .y = y});
}

bool LuaScriptHost::physicsOverlapBox(const float x, const float y, const float width, const float height, const std::string& ignoredEntityId) const {
  return world_ != nullptr && overlapBox(*world_, Vec2{.x = x, .y = y}, Vec2{.x = width, .y = height}, ignoredEntityId);
}

bool LuaScriptHost::physicsHasContact(const std::string& entityId, const PhysicsContactFilter2D& filter) const {
  return world_ != nullptr && hasContact(*world_, entityId, filter);
}

std::vector<PhysicsContact2D> LuaScriptHost::physicsContacts(const std::string& entityId) const {
  return world_ != nullptr ? contactsForEntity(*world_, entityId) : std::vector<PhysicsContact2D>{};
}

bool LuaScriptHost::createEntity(Entity entity) {
  if (world_ == nullptr || entity.id.empty()) {
    return false;
  }
  if (entity.name.empty()) {
    entity.name = entity.id;
  }
  if (Entity* existing = findEntity(*world_, entity.id)) {
    *existing = std::move(entity);
    return true;
  }
  world_->entities.push_back(std::move(entity));
  return true;
}

bool LuaScriptHost::setHudText(const std::string& id, const std::string& text) {
  if (world_ == nullptr) {
    return false;
  }
  for (HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      element.text = text;
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::setHudButtonLabel(const std::string& id, const std::string& label) {
  if (world_ == nullptr) {
    return false;
  }
  for (HudButtonElement& element : world_->hudButtons) {
    if (element.id == id) {
      element.label = label;
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::createHudText(const std::string& id, const std::string& text, const float x, const float y, const float scale, const Color color) {
  if (world_ == nullptr) {
    return false;
  }
  for (HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      element.text = text;
      element.position = Vec2{.x = x, .y = y};
      element.scale = scale;
      element.color = color;
      element.visible = true;
      return true;
    }
  }
  world_->hudText.push_back(HudTextElement{.id = id, .group = {}, .text = text, .position = Vec2{.x = x, .y = y}, .scale = scale, .color = color});
  return true;
}

bool LuaScriptHost::createHudRect(const std::string& id, const float x, const float y, const float width, const float height, const Color color) {
  if (world_ == nullptr) {
    return false;
  }
  for (HudRectElement& element : world_->hudRects) {
    if (element.id == id) {
      element.position = Vec2{.x = x, .y = y};
      element.size = Vec2{.x = width, .y = height};
      element.color = color;
      element.visible = true;
      return true;
    }
  }
  world_->hudRects.push_back(HudRectElement{.id = id, .group = {}, .position = Vec2{.x = x, .y = y}, .size = Vec2{.x = width, .y = height}, .color = color});
  return true;
}

bool LuaScriptHost::setHudRect(const std::string& id, const float x, const float y, const float width, const float height) {
  if (world_ == nullptr) {
    return false;
  }
  for (HudRectElement& element : world_->hudRects) {
    if (element.id == id) {
      element.position = Vec2{.x = x, .y = y};
      element.size = Vec2{.x = width, .y = height};
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::setHudColor(const std::string& id, const Color color) {
  if (world_ == nullptr) {
    return false;
  }
  for (HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      element.color = color;
      return true;
    }
  }
  for (HudRectElement& element : world_->hudRects) {
    if (element.id == id) {
      element.color = color;
      return true;
    }
  }
  for (HudButtonElement& element : world_->hudButtons) {
    if (element.id == id) {
      element.color = color;
      return true;
    }
  }
  return false;
}

bool LuaScriptHost::setHudVisible(const std::string& id, const bool visible) {
  if (world_ == nullptr) {
    return false;
  }
  bool changed = false;
  for (HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      element.visible = visible;
      changed = true;
    }
  }
  for (HudRectElement& element : world_->hudRects) {
    if (element.id == id) {
      element.visible = visible;
      changed = true;
    }
  }
  for (HudButtonElement& element : world_->hudButtons) {
    if (element.id == id) {
      element.visible = visible;
      changed = true;
    }
  }
  return changed;
}

bool LuaScriptHost::setHudGroupVisible(const std::string& group, const bool visible) {
  if (world_ == nullptr) {
    return false;
  }
  bool changed = false;
  for (HudTextElement& element : world_->hudText) {
    if (element.group == group) {
      element.visible = visible;
      changed = true;
    }
  }
  for (HudRectElement& element : world_->hudRects) {
    if (element.group == group) {
      element.visible = visible;
      changed = true;
    }
  }
  for (HudButtonElement& element : world_->hudButtons) {
    if (element.group == group) {
      element.visible = visible;
      changed = true;
    }
  }
  return changed;
}

std::optional<std::string> LuaScriptHost::hudText(const std::string& id) const {
  if (world_ == nullptr) {
    return std::nullopt;
  }
  for (const HudTextElement& element : world_->hudText) {
    if (element.id == id) {
      return element.text;
    }
  }
  return std::nullopt;
}

bool LuaScriptHost::isMouseDown(const std::string& button) const {
  return input_ != nullptr && input_->mouseButtonsDown.contains(normalizedKey(button));
}

Vec2 LuaScriptHost::mousePosition() const {
  return input_ != nullptr ? input_->mousePosition : Vec2{};
}

Vec2 LuaScriptHost::mouseWorldPosition() const {
  if (input_ == nullptr || world_ == nullptr) {
    return {};
  }
  const Camera2DComponent* camera = activeCamera(*world_);
  const Vec2 cameraPosition = activeCameraPosition(*world_);
  const float orthographicSize = camera != nullptr ? camera->orthographicSize : 10.0F;
  const float pixelsPerUnit = static_cast<float>(viewportHeight_) / std::max(orthographicSize * 2.0F, 1.0F);
  return Vec2{
    .x = cameraPosition.x + (input_->mousePosition.x - static_cast<float>(viewportWidth_) * 0.5F) / pixelsPerUnit,
    .y = cameraPosition.y + (static_cast<float>(viewportHeight_) * 0.5F - input_->mousePosition.y) / pixelsPerUnit,
  };
}

Vec2 LuaScriptHost::viewportSize() const {
  return Vec2{.x = static_cast<float>(viewportWidth_), .y = static_cast<float>(viewportHeight_)};
}

void LuaScriptHost::addDebugLine(const float x1, const float y1, const float x2, const float y2, const float r, const float g, const float b, const float a) {
  if (world_ == nullptr) {
    return;
  }
  world_->debugLines.push_back(DebugLine{.start = Vec2{.x = x1, .y = y1}, .end = Vec2{.x = x2, .y = y2}, .color = Color{.r = r, .g = g, .b = b, .a = a}});
}

void LuaScriptHost::clearDebugLines() {
  if (world_ != nullptr) {
    world_->debugLines.clear();
  }
}

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

void LuaScriptHost::setViewport(const int width, const int height) {
  viewportWidth_ = std::max(width, 1);
  viewportHeight_ = std::max(height, 1);
}

void LuaScriptHost::requestQuit() {
  quitRequested_ = true;
}

bool LuaScriptHost::quitRequested() const {
  return quitRequested_;
}

void LuaScriptHost::setWindowMode(std::string mode) {
  mode = normalizedKey(std::move(mode));
  if (mode != "windowed" && mode != "borderless" && mode != "fullscreen") {
    return;
  }
  if (windowMode_ == mode) {
    return;
  }
  windowMode_ = std::move(mode);
  windowModeDirty_ = true;
}

const std::string& LuaScriptHost::windowMode() const {
  return windowMode_;
}

bool LuaScriptHost::windowModeDirty() const {
  return windowModeDirty_;
}

void LuaScriptHost::clearWindowModeDirty() {
  windowModeDirty_ = false;
}

void LuaScriptHost::setMaxFps(const int maxFps) {
  if (maxFps <= 0) {
    maxFps_ = 0;
    return;
  }
  maxFps_ = std::clamp(maxFps, 15, 1000);
}

int LuaScriptHost::maxFps() const {
  return maxFps_;
}

void LuaScriptHost::setPhysicsEnabled(const bool enabled) {
  physicsEnabled_ = enabled;
}

bool LuaScriptHost::physicsEnabled() const {
  return physicsEnabled_;
}

std::uint64_t LuaScriptHost::addTimer(const float seconds, const bool repeating, const int callbackRef) {
#if !DEMI_HAS_LUA54
  (void)seconds;
  (void)repeating;
  (void)callbackRef;
  return 0;
#else
  if (state_ == nullptr || seconds < 0.0F || callbackRef == LUA_NOREF) {
    return 0;
  }
  const std::uint64_t id = nextTimerId_++;
  timers_.push_back(TimerInstance{.id = id, .remaining = seconds, .interval = std::max(seconds, 0.0F), .repeating = repeating, .callbackRef = callbackRef});
  return id;
#endif
}

bool LuaScriptHost::cancelTimer(const std::uint64_t timerId) {
  for (TimerInstance& timer : timers_) {
    if (timer.id == timerId && !timer.cancelled) {
      timer.cancelled = true;
      return true;
    }
  }
  return false;
}

std::uint64_t LuaScriptHost::addEventSubscription(std::string eventName, const int callbackRef) {
#if !DEMI_HAS_LUA54
  (void)eventName;
  (void)callbackRef;
  return 0;
#else
  if (state_ == nullptr || eventName.empty() || callbackRef == LUA_NOREF) {
    return 0;
  }
  const std::uint64_t id = nextEventSubscriptionId_++;
  eventSubscriptions_.push_back(EventSubscription{.id = id, .eventName = std::move(eventName), .callbackRef = callbackRef});
  return id;
#endif
}

bool LuaScriptHost::removeEventSubscription(const std::uint64_t subscriptionId) {
  for (EventSubscription& subscription : eventSubscriptions_) {
    if (subscription.id == subscriptionId && !subscription.cancelled) {
      subscription.cancelled = true;
      return true;
    }
  }
  return false;
}

int LuaScriptHost::emitEvent(const std::string& eventName, const int payloadIndex) {
#if !DEMI_HAS_LUA54
  (void)eventName;
  (void)payloadIndex;
  return 0;
#else
  auto* state = static_cast<lua_State*>(state_);
  if (state == nullptr || eventName.empty()) {
    return 0;
  }
  int delivered = 0;
  for (EventSubscription& subscription : eventSubscriptions_) {
    if (subscription.cancelled || subscription.eventName != eventName) {
      continue;
    }
    lua_rawgeti(state, LUA_REGISTRYINDEX, subscription.callbackRef);
    payloadIndex > 0 ? lua_pushvalue(state, payloadIndex) : lua_newtable(state);
    std::string error;
    if (!luaCall(state, 1, 0, error)) {
      luaReportCallbackError("Events.emit", {}, eventName, error);
    }
    ++delivered;
  }
  for (const ScriptInstance& script : scripts_) {
    for (const LuaEventHandler& handler : script.eventHandlers) {
      if (handler.eventName == eventName) {
        luaCallScriptEvent(state, script.tableRef, handler.functionName, payloadIndex, script.path, eventName);
        ++delivered;
      }
    }
  }
  for (const ModuleActionHandler& module : moduleActionHandlers_) {
    for (const LuaEventHandler& handler : module.eventHandlers) {
      if (handler.eventName == eventName) {
        luaCallModuleEvent(state, module.module, handler.functionName, payloadIndex, module.path, eventName);
        ++delivered;
      }
    }
  }
  return delivered;
#endif
}

void LuaScriptHost::dispatchHudEvents() {
#if DEMI_HAS_LUA54
  auto* state = static_cast<lua_State*>(state_);
  if (state == nullptr || world_ == nullptr || input_ == nullptr) {
    return;
  }
  const Vec2 mouse{
    .x = input_->mousePosition.x * std::max(world_->hudCanvasSize.x, 1.0F) / static_cast<float>(std::max(viewportWidth_, 1)),
    .y = input_->mousePosition.y * std::max(world_->hudCanvasSize.y, 1.0F) / static_cast<float>(std::max(viewportHeight_, 1)),
  };
  const bool mouseDown = isMouseDown("left");
  std::optional<std::string> clickedButtonId;
  for (HudButtonElement& button : world_->hudButtons) {
    if (!button.visible) {
      button.hovered = false;
      continue;
    }
    button.hovered = mouse.x >= button.position.x && mouse.x <= button.position.x + button.size.x && mouse.y >= button.position.y && mouse.y <= button.position.y + button.size.y;
    if (button.hovered && mouseDown && !previousUiMouseDown_) {
      clickedButtonId = button.id;
    }
  }

  for (const HudButtonElement& button : world_->hudButtons) {
    if (!button.visible || !button.hovered) {
      continue;
    }
    for (const ScriptInstance& script : scripts_) {
      if (script.entityId != button.id) {
        continue;
      }
      luaCallUiEvent(state, script.tableRef, "on_ui_hover", button, mouse, script.path);
      if (clickedButtonId.has_value() && *clickedButtonId == button.id) {
        luaCallUiEvent(state, script.tableRef, "on_ui_click", button, mouse, script.path);
      }
    }
    if (clickedButtonId.has_value() && *clickedButtonId == button.id && !button.action.empty()) {
      for (const ScriptInstance& script : scripts_) {
        for (const LuaActionHandler& handler : script.actionHandlers) {
          if (handler.action == button.action) {
            luaCallActionEvent(state, script.tableRef, handler.functionName, button, mouse, script.path);
          }
        }
      }
      for (const ModuleActionHandler& module : moduleActionHandlers_) {
        for (const LuaActionHandler& handler : module.actionHandlers) {
          if (handler.action == button.action) {
            luaCallModuleActionEvent(state, module.module, handler.functionName, button, mouse, module.path);
          }
        }
      }
      lua_newtable(state);
      lua_pushstring(state, button.id.c_str()); lua_setfield(state, -2, "id");
      lua_pushstring(state, button.label.c_str()); lua_setfield(state, -2, "label");
      lua_pushstring(state, button.action.c_str()); lua_setfield(state, -2, "action");
      lua_pushnumber(state, mouse.x); lua_setfield(state, -2, "mouse_x");
      lua_pushnumber(state, mouse.y); lua_setfield(state, -2, "mouse_y");
      const int payloadIndex = lua_gettop(state);
      (void)emitEvent("hud_action", payloadIndex);
      lua_pop(state, 1);
    }
  }
  previousUiMouseDown_ = mouseDown;
#endif
}

void LuaScriptHost::updateTimers(const float dt) {
#if DEMI_HAS_LUA54
  auto* state = static_cast<lua_State*>(state_);
  if (state == nullptr) {
    return;
  }
  for (TimerInstance& timer : timers_) {
    if (timer.cancelled) {
      continue;
    }
    timer.remaining -= std::max(dt, 0.0F);
    if (timer.remaining > 0.0F) {
      continue;
    }
    lua_rawgeti(state, LUA_REGISTRYINDEX, timer.callbackRef);
    lua_pushinteger(state, static_cast<lua_Integer>(timer.id));
    std::string error;
    if (!luaCall(state, 1, 0, error)) {
      luaReportCallbackError("Timer", {}, std::to_string(timer.id), error);
    }
    if (timer.repeating && !timer.cancelled) {
      timer.remaining += std::max(timer.interval, 0.0001F);
    } else {
      timer.cancelled = true;
    }
  }
  std::erase_if(timers_, [&](const TimerInstance& timer) {
    if (!timer.cancelled) {
      return false;
    }
    luaL_unref(state, LUA_REGISTRYINDEX, timer.callbackRef);
    return true;
  });
#else
  (void)dt;
#endif
}

void LuaScriptHost::clearTimersAndEvents() {
#if DEMI_HAS_LUA54
  auto* state = static_cast<lua_State*>(state_);
  if (state != nullptr) {
    for (const TimerInstance& timer : timers_) {
      luaL_unref(state, LUA_REGISTRYINDEX, timer.callbackRef);
    }
    for (const EventSubscription& subscription : eventSubscriptions_) {
      luaL_unref(state, LUA_REGISTRYINDEX, subscription.callbackRef);
    }
    for (const SaveMigrationHook& hook : saveMigrationHooks_) {
      luaL_unref(state, LUA_REGISTRYINDEX, hook.callbackRef);
    }
  }
#endif
  timers_.clear();
  eventSubscriptions_.clear();
  saveMigrationHooks_.clear();
}

} // namespace demi::runtime
