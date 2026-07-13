#pragma once

#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/isometric/IsoGridApi.h"
#include "demi/runtime/network/NetworkSystem.h"
#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/scene/SceneData.h"
#include "demi/runtime/simulation/DeterministicRandom.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

class AudioSystem;
class MediaSystem;
class NetworkSystem;

class LuaScriptHost {
public:
  struct SaveValue {
    std::string value;
    bool number = false;
  };

  struct SaveMigrationHook {
    int fromVersion = 0;
    int toVersion = 0;
    int callbackRef = 0;
  };

  LuaScriptHost();
  ~LuaScriptHost();

  LuaScriptHost(const LuaScriptHost &) = delete;
  LuaScriptHost &operator=(const LuaScriptHost &) = delete;

  [[nodiscard]] bool initialize(World &world, const InputState &input,
                                AudioSystem *audio, std::string &error);
  void setMediaSystem(MediaSystem *media);
  void setNetworkSystem(NetworkSystem *network);
  [[nodiscard]] std::filesystem::path
  resolveProjectPath(const std::string &path) const;
  [[nodiscard]] bool loadWorldScripts(const ProjectData &project, World &world,
                                      std::string &error);
  void requestSceneLoad(const std::string &sceneId);
  [[nodiscard]] bool hasPendingSceneLoad() const;
  [[nodiscard]] bool applyPendingSceneLoad(std::string &error);
  [[nodiscard]] bool isKeyDown(const std::string &key) const;
  [[nodiscard]] bool isKeyPressed(const std::string &key) const;
  [[nodiscard]] bool isActionDown(const std::string &action) const;
  [[nodiscard]] bool isActionPressed(const std::string &action) const;
  [[nodiscard]] float actionValue(const std::string &action) const;
  void seedRandom(std::uint64_t seed);
  [[nodiscard]] std::uint64_t randomState() const;
  void restoreRandomState(std::uint64_t state);
  [[nodiscard]] float randomValue();
  [[nodiscard]] float randomRange(float minimum, float maximum);
  [[nodiscard]] int randomInteger(int minimum, int maximum);
  [[nodiscard]] isometric::IsoGridApi &isoGridApi();
  [[nodiscard]] std::string textEntered() const;
  [[nodiscard]] bool addEntityPosition(const std::string &entityId, float dx,
                                       float dy);
  [[nodiscard]] bool setEntityPosition(const std::string &entityId, float x,
                                       float y);
  [[nodiscard]] std::optional<Vec2>
  entityPosition(const std::string &entityId) const;
  [[nodiscard]] std::optional<float>
  entityRotation(const std::string &entityId) const;
  [[nodiscard]] bool setEntityRotation(const std::string &entityId,
                                       float rotation);
  [[nodiscard]] std::optional<Vec2>
  entityScale(const std::string &entityId) const;
  [[nodiscard]] bool setEntityScale(const std::string &entityId, float x,
                                    float y);
  [[nodiscard]] bool addEntityPosition3D(const std::string &entityId, float dx,
                                         float dy, float dz);
  [[nodiscard]] bool setEntityPosition3D(const std::string &entityId, float x,
                                         float y, float z);
  [[nodiscard]] std::optional<Vec3>
  entityPosition3D(const std::string &entityId) const;
  [[nodiscard]] std::optional<Vec3>
  entityRotation3D(const std::string &entityId) const;
  [[nodiscard]] bool setEntityRotation3D(const std::string &entityId, float x,
                                         float y, float z);
  [[nodiscard]] std::optional<Vec3>
  entityScale3D(const std::string &entityId) const;
  [[nodiscard]] bool setEntityScale3D(const std::string &entityId, float x,
                                      float y, float z);
  [[nodiscard]] std::optional<std::string>
  findEntityId(const std::string &idOrName) const;
  [[nodiscard]] bool destroyEntity(const std::string &entityId);
  [[nodiscard]] int destroyEntities(const std::vector<std::string> &entityIds);
  [[nodiscard]] bool setEntitySpriteColor(const std::string &entityId,
                                          Color color);
  [[nodiscard]] bool playSpriteAnimation(const std::string &entityId,
                                         const std::string &clip, bool restart);
  [[nodiscard]] bool setSpriteAnimationPlaying(const std::string &entityId,
                                               bool playing);
  [[nodiscard]] std::optional<std::string>
  spriteAnimationClip(const std::string &entityId) const;
  [[nodiscard]] bool setSpriteFlip(const std::string &entityId, bool flipX,
                                   bool flipY);
  [[nodiscard]] bool setSpriteSize(const std::string &entityId, float width,
                                   float height);
  [[nodiscard]] std::optional<std::string>
  animationState(const std::string &entityId) const;
  [[nodiscard]] bool playAnimationState(const std::string &entityId,
                                        const std::string &state);
  [[nodiscard]] bool setAnimationParameter(const std::string &entityId,
                                           const std::string &parameter,
                                           float value);
  [[nodiscard]] bool triggerAnimation(const std::string &entityId,
                                      const std::string &trigger);
  [[nodiscard]] std::optional<Vec2>
  getRigidbodyVelocity(const std::string &entityId) const;
  [[nodiscard]] bool setRigidbodyVelocity(const std::string &entityId, float x,
                                          float y);
  [[nodiscard]] bool setRigidbodyVelocityX(const std::string &entityId,
                                           float x);
  [[nodiscard]] bool setRigidbodyVelocityY(const std::string &entityId,
                                           float y);
  [[nodiscard]] bool addRigidbodyImpulse(const std::string &entityId, float x,
                                         float y);
  [[nodiscard]] bool
  physicsOverlapBox(float x, float y, float width, float height,
                    const std::string &ignoredEntityId) const;
  [[nodiscard]] std::vector<std::string>
  physicsOverlapCircle(float x, float y, float radius, const std::string &layer,
                       const std::string &ignoredEntityId) const;
  [[nodiscard]] std::optional<PhysicsRaycastHit2D>
  physicsRaycast(float originX, float originY, float directionX,
                 float directionY, float distance, const std::string &layer,
                 const std::string &ignoredEntityId) const;
  [[nodiscard]] bool
  physicsHasContact(const std::string &entityId,
                    const PhysicsContactFilter2D &filter) const;
  [[nodiscard]] std::vector<PhysicsContact2D>
  physicsContacts(const std::string &entityId) const;
  [[nodiscard]] bool createEntity(Entity entity);
  [[nodiscard]] bool setEntityMeshRenderer(const std::string &entityId,
                                           std::string texture,
                                           std::vector<Vec3> vertices,
                                           std::vector<Vec3> normals,
                                           std::vector<Vec2> uvs);
  [[nodiscard]] bool setHudText(const std::string &id, const std::string &text);
  [[nodiscard]] bool setHudButtonLabel(const std::string &id,
                                       const std::string &label);
  [[nodiscard]] bool createHudText(const std::string &id,
                                   const std::string &text, float x, float y,
                                   float scale, Color color);
  [[nodiscard]] bool setHudTextScale(const std::string &id, float scale);
  [[nodiscard]] bool createHudRect(const std::string &id, float x, float y,
                                   float width, float height, Color color);
  [[nodiscard]] bool setHudRect(const std::string &id, float x, float y,
                                float width, float height);
  [[nodiscard]] bool setHudImage(const std::string &id, std::string texture,
                                 float sourceX, float sourceY,
                                 float sourceWidth, float sourceHeight);
  [[nodiscard]] bool setHudImageAnimationFrame(const std::string &id,
                                               std::string animation,
                                               int frame);
  [[nodiscard]] bool setHudPosition(const std::string &id, float x, float y);
  [[nodiscard]] bool setHudSize(const std::string &id, float width,
                                float height);
  [[nodiscard]] bool setHudColor(const std::string &id, Color color);
  [[nodiscard]] bool setHudOpacity(const std::string &id, float opacity);
  [[nodiscard]] bool setHudVisible(const std::string &id, bool visible);
  [[nodiscard]] bool setHudValue(const std::string &id, float value);
  [[nodiscard]] bool setHudChecked(const std::string &id, bool checked);
  [[nodiscard]] bool setHudDisabled(const std::string &id, bool disabled);
  [[nodiscard]] bool focusNextHudControl(bool reverse);
  [[nodiscard]] std::string focusedHudControl() const;
  [[nodiscard]] bool setHudGroupVisible(const std::string &group, bool visible);
  [[nodiscard]] std::optional<std::string> hudText(const std::string &id) const;
  [[nodiscard]] std::optional<float> saveNumber(const std::string &slot,
                                                const std::string &key);
  [[nodiscard]] std::optional<std::string> saveString(const std::string &slot,
                                                      const std::string &key);
  [[nodiscard]] bool setSaveNumber(const std::string &slot,
                                   const std::string &key, float value);
  [[nodiscard]] bool setSaveString(const std::string &slot,
                                   const std::string &key,
                                   const std::string &value);
  [[nodiscard]] bool saveExists(const std::string &slot) const;
  [[nodiscard]] bool deleteSave(const std::string &slot);
  [[nodiscard]] std::optional<std::string>
  readSaveDocument(const std::string &slot) const;
  [[nodiscard]] bool writeSaveDocument(const std::string &slot,
                                       const std::string &stateJson,
                                       int formatVersion);
  [[nodiscard]] int saveFormatVersion(const std::string &slot) const;
  [[nodiscard]] bool writeGameSaveDocument(const std::string &slot,
                                           const std::string &stateJson,
                                           int formatVersion, bool autosave,
                                           int sequence,
                                           const std::string &reason);
  [[nodiscard]] std::optional<std::string>
  readGameSaveDocument(const std::string &slot);
  [[nodiscard]] const std::string &lastSaveError() const;
  void addSaveMigrationHook(int fromVersion, int toVersion, int callbackRef);
  [[nodiscard]] const std::vector<SaveMigrationHook> &
  saveMigrationHooks() const;
  [[nodiscard]] bool isMouseDown(const std::string &button) const;
  [[nodiscard]] Vec2 mousePosition() const;
  [[nodiscard]] Vec2 mouseDelta() const;
  [[nodiscard]] Vec2 mouseWorldPosition() const;
  [[nodiscard]] Vec2 viewportSize() const;
  void addDebugLine(float x1, float y1, float x2, float y2, float r, float g,
                    float b, float a);
  void clearDebugLines();
  [[nodiscard]] std::uint64_t playAudio(const std::string &assetId);
  [[nodiscard]] std::uint64_t playAudioSource(const std::string &entityId);
  [[nodiscard]] bool stopAudioSource(const std::string &entityId);
  [[nodiscard]] bool stopAudio(std::uint64_t handle);
  void setMasterVolume(float volume);
  [[nodiscard]] float masterVolume() const;
  [[nodiscard]] std::uint64_t playVideo(const std::string &assetId, bool loop);
  [[nodiscard]] std::uint64_t playVideoPlayer(const std::string &entityId);
  [[nodiscard]] bool stopVideo(std::uint64_t handle);
  [[nodiscard]] bool isVideoPlaying(std::uint64_t handle) const;
  [[nodiscard]] bool networkAvailable() const;
  [[nodiscard]] bool networkHost(std::uint16_t port, std::uint32_t maxPeers);
  [[nodiscard]] bool networkHostSecure(std::uint16_t port,
                                       const std::string &certificate,
                                       const std::string &privateKey,
                                       std::uint32_t maxPeers);
  [[nodiscard]] bool networkConnect(const std::string &address,
                                    std::uint16_t port);
  [[nodiscard]] bool networkConnectSecure(const std::string &address,
                                          std::uint16_t port,
                                          const std::string &trustedCertificate,
                                          const std::string &serverName);
  void networkDisconnect();
  void networkDisconnectPeer(std::uint32_t peerId);
  [[nodiscard]] bool networkSend(const std::string &message, bool reliable,
                                 std::uint8_t channel, std::uint32_t peerId);
  [[nodiscard]] bool networkIsHost() const;
  [[nodiscard]] bool networkIsConnected() const;
  [[nodiscard]] std::uint32_t networkLatencyMs() const;
  [[nodiscard]] bool networkIsSecure() const;
  [[nodiscard]] std::string networkSecurityError() const;
  [[nodiscard]] std::vector<NetworkEvent> networkDrainEvents();
  [[nodiscard]] bool startCutscene(std::string id);
  [[nodiscard]] bool pauseCutscene();
  [[nodiscard]] bool resumeCutscene();
  [[nodiscard]] bool stopCutscene();
  [[nodiscard]] bool isCutscenePlaying() const;
  [[nodiscard]] const std::string &activeCutscene() const;
  void setViewport(int width, int height);
  void requestQuit();
  [[nodiscard]] bool quitRequested() const;
  void setWindowMode(std::string mode);
  [[nodiscard]] const std::string &windowMode() const;
  [[nodiscard]] bool windowModeDirty() const;
  void clearWindowModeDirty();
  void setMaxFps(int maxFps);
  [[nodiscard]] int maxFps() const;
  void setMouseCaptured(bool captured);
  [[nodiscard]] bool mouseCaptured() const;
  [[nodiscard]] bool mouseCapturedDirty() const;
  void clearMouseCapturedDirty();
  void setPhysicsEnabled(bool enabled);
  [[nodiscard]] bool physicsEnabled() const;
  [[nodiscard]] std::uint64_t addTimer(float seconds, bool repeating,
                                       int callbackRef);
  [[nodiscard]] bool cancelTimer(std::uint64_t timerId);
  [[nodiscard]] std::uint64_t addEventSubscription(std::string eventName,
                                                   int callbackRef);
  [[nodiscard]] bool removeEventSubscription(std::uint64_t subscriptionId);
  [[nodiscard]] int emitEvent(const std::string &eventName, int payloadIndex);
  void start();
  void update(float dt);
  void fixedUpdate(float dt);
  void destroy();

  [[nodiscard]] static Diagnostics
  checkScriptSyntax(const std::filesystem::path &path);

private:
  struct ScriptInstance {
    std::string entityId;
    std::string module;
    std::filesystem::path path;
    std::filesystem::file_time_type lastWriteTime{};
    int tableRef = 0;
    std::vector<LuaActionHandler> actionHandlers;
    std::vector<LuaEventHandler> eventHandlers;
  };

  struct TimerInstance {
    std::uint64_t id = 0;
    float remaining = 0.0F;
    float interval = 0.0F;
    bool repeating = false;
    bool cancelled = false;
    int callbackRef = 0;
  };

  struct EventSubscription {
    std::uint64_t id = 0;
    std::string eventName;
    bool cancelled = false;
    int callbackRef = 0;
  };

  struct ModuleActionHandler {
    std::string module;
    std::filesystem::path path;
    std::filesystem::file_time_type lastWriteTime{};
    std::vector<LuaActionHandler> actionHandlers;
    std::vector<LuaEventHandler> eventHandlers;
  };

  void dispatchHudEvents();
  void dispatchAnimationEvents();
  void dispatchAnimationCollisionEvents();
  void dispatchPhysicsEvents();
  void updateTimers(float dt);
  void reloadChangedScripts();
  void unloadScripts();
  void clearTimersAndEvents();
  [[nodiscard]] std::unordered_map<std::string, SaveValue> &
  loadSaveSlot(const std::string &slot);
  [[nodiscard]] bool writeSaveSlot(const std::string &slot);

  void *state_ = nullptr;
  World *world_ = nullptr;
  const ProjectData *project_ = nullptr;
  const InputState *input_ = nullptr;
  input::InputActionMap inputActions_;
  simulation::DeterministicRandom random_;
  isometric::IsoGridApi isoGridApi_;
  AudioSystem *audio_ = nullptr;
  MediaSystem *media_ = nullptr;
  NetworkSystem *network_ = nullptr;
  std::filesystem::path projectDirectory_;
  int viewportWidth_ = 1;
  int viewportHeight_ = 1;
  bool quitRequested_ = false;
  std::string windowMode_ = "windowed";
  bool windowModeDirty_ = false;
  int maxFps_ = 0;
  bool mouseCaptured_ = false;
  bool mouseCapturedDirty_ = false;
  bool physicsEnabled_ = true;
  bool hotReloadEnabled_ = false;
  bool cutscenePaused_ = false;
  bool previousUiMouseDown_ = false;
  std::optional<std::string> pendingSceneLoad_;
  std::string activeCutscene_;
  std::unordered_map<std::string, std::unordered_map<std::string, SaveValue>>
      saves_;
  std::vector<ScriptInstance> scripts_;
  std::vector<ModuleActionHandler> moduleActionHandlers_;
  std::vector<TimerInstance> timers_;
  std::vector<EventSubscription> eventSubscriptions_;
  std::vector<SaveMigrationHook> saveMigrationHooks_;
  std::string lastSaveError_;
  std::uint64_t nextTimerId_ = 1;
  std::uint64_t nextMeshRevision_ = 1;
  std::uint64_t nextEventSubscriptionId_ = 1;
};

} // namespace demi::runtime
