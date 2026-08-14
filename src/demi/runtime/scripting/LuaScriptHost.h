#pragma once

#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/data/DataAssetStore.h"
#include "demi/runtime/input/TouchGestureRecognizer.h"
#include "demi/runtime/isometric/IsoGridApi.h"
#include "demi/runtime/navigation/NavigationGrid2D.h"
#include "demi/runtime/network/NetworkContract.h"
#include "demi/runtime/network/NetworkSystem.h"
#include "demi/runtime/physics/Physics2D.h"
#include "demi/runtime/physics/SpatialQuery3D.h"
#include "demi/runtime/platform/ApplicationServices.h"
#include "demi/runtime/scene/RuntimeObjectModel.h"
#include "demi/runtime/scene/RuntimePrefabService.h"
#include "demi/runtime/scene/SceneFlow.h"
#include "demi/runtime/scene/WorldCommandBuffer.h"
#include "demi/runtime/scene/model/ProjectData.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/simulation/DeterministicRandom.h"
#include "demi/runtime/tilemap/TilemapRuntime.h"
#include "demi/runtime/ui/UiAccessibilityTree.h"
#include "demi/runtime/ui/UiModel.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace demi::runtime {

struct AudioPlaybackRequest;
class AudioSystem;
class MediaSystem;
class NetworkSystem;
class RuntimeAssetService;

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

  [[nodiscard]] bool initialize(World &world, InputState &input,
                                AudioSystem *audio, std::string &error);
  void setMediaSystem(MediaSystem *media);
  void setNetworkSystem(NetworkSystem *network);
  void setRuntimeAssetService(RuntimeAssetService *assets);
  [[nodiscard]] RuntimeAssetService *runtimeAssetService() const;
  void setAssetRegistry(const demi::AssetRegistry *assets);
  [[nodiscard]] const NetworkContract *networkContract() const;
  [[nodiscard]] DataAssetStore &dataAssetStore();
  [[nodiscard]] const DataAssetStore &dataAssetStore() const;
  [[nodiscard]] std::filesystem::path
  resolveProjectPath(const std::string &path) const;
  [[nodiscard]] bool loadWorldScripts(const ProjectData &project, World &world,
                                      std::string &error);
  [[nodiscard]] bool requestSceneLoad(const std::string &sceneId);
  [[nodiscard]] bool hasPendingSceneLoad();
  [[nodiscard]] bool applyPendingSceneLoad(std::string &error);
  [[nodiscard]] bool prepareScene(const std::string &sceneId, bool additive);
  [[nodiscard]] bool cancelScenePreparation();
  [[nodiscard]] float scenePreparationProgress();
  [[nodiscard]] bool scenePrepared();
  [[nodiscard]] bool requestPreparedSceneActivation();
  [[nodiscard]] bool requestSceneUnload(const std::string &sceneId);
  [[nodiscard]] bool requestSceneReload();
  [[nodiscard]] bool setEntityPersistent(const std::string &entityId,
                                         bool persistent);
  [[nodiscard]] std::string activeSceneId() const;
  [[nodiscard]] std::string sceneFlowError() const;
  [[nodiscard]] std::optional<std::string>
  instantiatePrefab(const std::string &prefab,
                    const PrefabInstantiateOptions &options);
  [[nodiscard]] bool releasePrefab(const std::string &instanceId);
  [[nodiscard]] std::size_t pooledPrefabCount(const std::string &prefab) const;
  [[nodiscard]] bool isKeyDown(const std::string &key) const;
  [[nodiscard]] bool isKeyPressed(const std::string &key) const;
  [[nodiscard]] bool isKeyReleased(const std::string &key) const;
  [[nodiscard]] bool isActionDown(const std::string &action,
                                  int player = -1) const;
  [[nodiscard]] bool isActionPressed(const std::string &action,
                                     int player = -1) const;
  [[nodiscard]] bool isActionReleased(const std::string &action,
                                      int player = -1) const;
  [[nodiscard]] float actionValue(const std::string &action,
                                  int player = -1) const;
  [[nodiscard]] Vec2 actionVector(const std::string &action,
                                  int player = -1) const;
  [[nodiscard]] std::string actionSource(const std::string &action,
                                         int player = -1) const;
  void enableInputContext(const std::string &context);
  void disableInputContext(const std::string &context);
  [[nodiscard]] bool inputContextEnabled(const std::string &context) const;
  [[nodiscard]] bool rebindInput(const std::string &action,
                                 std::size_t bindingIndex,
                                 const std::string &input, int player,
                                 std::string &error);
  [[nodiscard]] bool saveInputBindings(const std::string &path,
                                       std::string &error) const;
  [[nodiscard]] bool loadInputBindings(const std::string &path,
                                       std::string &error);
  [[nodiscard]] bool assignGamepad(int deviceId, int player);
  [[nodiscard]] const InputState *inputState() const;
  [[nodiscard]] const std::vector<input::GestureEvent> &gestures() const;
  void seedRandom(std::uint64_t seed);
  [[nodiscard]] std::uint64_t randomState() const;
  void restoreRandomState(std::uint64_t state);
  [[nodiscard]] float randomValue();
  [[nodiscard]] float randomRange(float minimum, float maximum);
  [[nodiscard]] int randomInteger(int minimum, int maximum);
  [[nodiscard]] isometric::IsoGridApi &isoGridApi();
  [[nodiscard]] navigation::NavigationGrid2D &navigationGrid2D();
  [[nodiscard]] TilemapRuntime &tilemapRuntime();
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
  [[nodiscard]] std::optional<Vec3>
  entityForward3D(const std::string &entityId) const;
  [[nodiscard]] std::optional<Vec3>
  entityRight3D(const std::string &entityId) const;
  [[nodiscard]] std::optional<Vec3>
  entityUp3D(const std::string &entityId) const;
  [[nodiscard]] bool lookAtEntity3D(const std::string &entityId, float x,
                                    float y, float z);
  [[nodiscard]] std::optional<CameraRay3D>
  cameraRay3D(const std::string &entityId, float screenX, float screenY,
              float viewportWidth, float viewportHeight) const;
  [[nodiscard]] std::optional<Vec2>
  cameraWorldToScreen3D(const std::string &entityId, float worldX, float worldY,
                        float worldZ, float viewportWidth,
                        float viewportHeight) const;
  [[nodiscard]] std::optional<Vec3>
  cameraScreenToWorld3D(const std::string &entityId, float screenX,
                        float screenY, float viewportWidth,
                        float viewportHeight, float distance) const;
  [[nodiscard]] std::optional<std::string>
  findEntityId(const std::string &idOrName) const;
  [[nodiscard]] bool entityExists(const std::string &entityId) const;
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
  [[nodiscard]] bool setSpriteLayer(const std::string &entityId,
                                    const std::string &layer);
  [[nodiscard]] bool setSpriteSortingOrder(const std::string &entityId,
                                           int sortingOrder);
  [[nodiscard]] bool setSpriteMaterial(const std::string &entityId,
                                       const std::string &material);
  [[nodiscard]] std::optional<std::string>
  animationState(const std::string &entityId) const;
  [[nodiscard]] bool playAnimationState(const std::string &entityId,
                                        const std::string &state);
  [[nodiscard]] bool setAnimationParameter(const std::string &entityId,
                                           const std::string &parameter,
                                           float value);
  [[nodiscard]] bool triggerAnimation(const std::string &entityId,
                                      const std::string &trigger);
  [[nodiscard]] bool setAnimationSpeed(const std::string &entityId,
                                       float speed);
  [[nodiscard]] float
  animationNormalizedTime(const std::string &entityId) const;
  [[nodiscard]] std::string
  animationTransitionFrom(const std::string &entityId) const;
  [[nodiscard]] std::string
  animationTransitionTo(const std::string &entityId) const;
  [[nodiscard]] float
  animationTransitionProgress(const std::string &entityId) const;
  [[nodiscard]] bool setAnimationLayerWeight(const std::string &entityId,
                                             const std::string &layer,
                                             float weight);
  [[nodiscard]] bool setAnimationRootMotion(const std::string &entityId,
                                            bool enabled);
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
  [[nodiscard]] bool addRigidbodyForce(const std::string &entityId, float x,
                                       float y);
  [[nodiscard]] bool addRigidbodyTorque(const std::string &entityId,
                                        float torque);
  [[nodiscard]] bool setRigidbodyAngularVelocity(const std::string &entityId,
                                                 float angularVelocity);
  [[nodiscard]] bool setRigidbodyAwake(const std::string &entityId, bool awake);
  [[nodiscard]] bool setRigidbodyEnabled(const std::string &entityId,
                                         bool enabled);
  [[nodiscard]] bool moveKinematicBody(const std::string &entityId, float x,
                                       float y, float fixedDt);
  [[nodiscard]] std::optional<Vec3>
  getRigidbodyVelocity3D(const std::string &entityId) const;
  [[nodiscard]] bool setRigidbodyVelocity3D(const std::string &entityId,
                                            float x, float y, float z);
  [[nodiscard]] bool addRigidbodyImpulse3D(const std::string &entityId, float x,
                                           float y, float z);
  [[nodiscard]] bool addRigidbodyForce3D(const std::string &entityId, float x,
                                         float y, float z);
  [[nodiscard]] bool addRigidbodyTorque3D(const std::string &entityId, float x,
                                          float y, float z);
  [[nodiscard]] bool setRigidbodyAwake3D(const std::string &entityId,
                                         bool awake);
  [[nodiscard]] bool setRigidbodyEnabled3D(const std::string &entityId,
                                           bool enabled);
  [[nodiscard]] bool moveKinematicBody3D(const std::string &entityId, float x,
                                         float y, float z, float rotationX,
                                         float rotationY, float rotationZ,
                                         float fixedDt);
  [[nodiscard]] bool setCharacterVelocity3D(const std::string &entityId,
                                            float x, float y, float z);
  [[nodiscard]] bool requestCharacterJump3D(const std::string &entityId,
                                            float speed);
  [[nodiscard]] std::optional<CharacterMoveResult3D>
  characterState3D(const std::string &entityId) const;
  [[nodiscard]] std::optional<Vec2>
  moveAndSlideKinematic(const std::string &entityId, float x, float y);
  [[nodiscard]] bool
  physicsOverlapBox(float x, float y, float width, float height,
                    const std::string &ignoredEntityId) const;
  [[nodiscard]] std::vector<std::string>
  physicsOverlapCircle(float x, float y, float radius, const std::string &layer,
                       const std::string &ignoredEntityId) const;
  [[nodiscard]] std::vector<PhysicsQueryHit2D>
  physicsOverlapBoxAll(float x, float y, float width, float height,
                       const std::string &layer,
                       const std::string &ignoredEntityId) const;
  [[nodiscard]] std::vector<PhysicsQueryHit2D>
  physicsOverlapCircleAll(float x, float y, float radius,
                          const std::string &layer,
                          const std::string &ignoredEntityId) const;
  [[nodiscard]] std::optional<PhysicsRaycastHit2D>
  physicsRaycast(float originX, float originY, float directionX,
                 float directionY, float distance, const std::string &layer,
                 const std::string &ignoredEntityId) const;
  [[nodiscard]] std::vector<std::string>
  physicsOverlapSphere3D(float x, float y, float z, float radius,
                         const std::string &ignoredEntityId) const;
  [[nodiscard]] std::vector<PhysicsQueryHit3D>
  physicsOverlapSphereAll3D(float x, float y, float z, float radius,
                            const std::string &layer,
                            const std::string &ignoredEntityId) const;
  [[nodiscard]] std::vector<PhysicsQueryHit3D>
  physicsOverlapBoxAll3D(float x, float y, float z, float width, float height,
                         float depth, const std::string &layer,
                         const std::string &ignoredEntityId) const;
  [[nodiscard]] std::optional<PhysicsRaycastHit3D>
  physicsRaycast3D(float originX, float originY, float originZ,
                   float directionX, float directionY, float directionZ,
                   float distance, const std::string &ignoredEntityId) const;
  [[nodiscard]] std::optional<PhysicsQueryHit3D>
  physicsSphereCast3D(float originX, float originY, float originZ, float radius,
                      float directionX, float directionY, float directionZ,
                      float distance, const std::string &layer,
                      const std::string &ignoredEntityId) const;
  [[nodiscard]] bool
  physicsHasContact(const std::string &entityId,
                    const PhysicsContactFilter2D &filter) const;
  [[nodiscard]] std::vector<PhysicsContact2D>
  physicsContacts(const std::string &entityId) const;
  [[nodiscard]] bool createEntity(Entity entity);
  [[nodiscard]] bool replaceEntity(Entity entity);
  [[nodiscard]] bool cloneEntity(const std::string &sourceId,
                                 const std::string &newId);
  [[nodiscard]] bool setEntityEnabled(const std::string &entityId,
                                      bool enabled);
  [[nodiscard]] bool isEntityEnabled(const std::string &entityId) const;
  [[nodiscard]] bool addEntityComponent(const std::string &entityId,
                                        const std::string &component,
                                        const nlohmann::json &values);
  [[nodiscard]] bool removeEntityComponent(const std::string &entityId,
                                           const std::string &component);
  [[nodiscard]] bool hasEntityComponent(const std::string &entityId,
                                        const std::string &component) const;
  [[nodiscard]] std::optional<nlohmann::json>
  entityComponentField(const std::string &entityId,
                       const std::string &component,
                       const std::string &field) const;
  [[nodiscard]] bool setEntityComponentField(const std::string &entityId,
                                             const std::string &component,
                                             const std::string &field,
                                             const nlohmann::json &value);
  [[nodiscard]] std::vector<std::string>
  queryEntities(const EntityQuery &query) const;
  [[nodiscard]] bool
  setEntityParent(const std::string &entityId,
                  const std::optional<std::string> &parentId);
  [[nodiscard]] std::optional<std::string>
  entityParent(const std::string &entityId) const;
  [[nodiscard]] std::vector<std::string>
  entityChildren(const std::string &entityId) const;
  [[nodiscard]] std::optional<nlohmann::json>
  entityLocalPosition(const std::string &entityId) const;
  [[nodiscard]] std::optional<nlohmann::json>
  entityWorldPosition(const std::string &entityId) const;
  [[nodiscard]] std::optional<std::string>
  captureEntityReplicatedState(const std::string &entityId) const;
  [[nodiscard]] std::optional<std::string> captureEntityReplicatedState(
      const std::string &entityId, const NetworkContract &contract,
      std::string_view prefabKey, NetworkActor writer) const;
  [[nodiscard]] std::string
  applyEntityReplicatedState(const std::string &entityId,
                             const std::string &stateJson);
  [[nodiscard]] bool
  setEntityMeshRenderer(const std::string &entityId, std::string texture,
                        std::string material, std::string renderLayer,
                        std::vector<Vec3> vertices, std::vector<Vec3> normals,
                        std::vector<Vec2> uvs);
  [[nodiscard]] bool setHudText(const std::string &id, const std::string &text);
  [[nodiscard]] bool setHudFont(const std::string &id, std::string font);
  [[nodiscard]] bool setHudFontSize(const std::string &id, float fontSize);
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
  [[nodiscard]] bool setHudBackgroundColor(const std::string &id, Color color);
  [[nodiscard]] bool setHudOpacity(const std::string &id, float opacity);
  [[nodiscard]] bool setHudVisible(const std::string &id, bool visible);
  [[nodiscard]] bool setHudValue(const std::string &id, float value);
  [[nodiscard]] bool setHudChecked(const std::string &id, bool checked);
  [[nodiscard]] bool setHudDisabled(const std::string &id, bool disabled);
  [[nodiscard]] bool focusNextHudControl(bool reverse);
  [[nodiscard]] std::string focusedHudControl() const;
  [[nodiscard]] Vec2 hudCanvasSize() const;
  [[nodiscard]] std::optional<std::string> hudText(const std::string &id) const;
  [[nodiscard]] std::optional<ui::UiNodeHandle>
  createHudNode(const std::string &parent, ui::UiNode node, std::string &error);
  [[nodiscard]] std::optional<ui::UiNodeHandle>
  hudNodeHandle(const std::string &id) const;
  [[nodiscard]] std::optional<ui::UiNodeHandle>
  cloneHudNode(const ui::UiNodeHandle &source, const std::string &newRootId,
               const std::string &parent, std::string &error);
  [[nodiscard]] bool removeHudNode(const ui::UiNodeHandle &node,
                                   std::string &error);
  [[nodiscard]] bool reparentHudNode(const ui::UiNodeHandle &node,
                                     const std::string &parent,
                                     std::string &error);
  [[nodiscard]] bool clearHudChildren(const std::string &parent,
                                      std::string &error);
  [[nodiscard]] std::vector<std::string>
  hudChildren(const std::string &parent) const;
  [[nodiscard]] ui::UiVirtualReconcileResult
  reconcileHudRows(const std::string &collectionId,
                   const ui::UiNodeHandle &rowTemplate,
                   const std::vector<std::string> &stableKeys,
                   const std::vector<float> &rowExtents, float scrollOffset,
                   float viewportExtent, std::size_t overscan);
  [[nodiscard]] bool clearHudRows(const std::string &collectionId);
  [[nodiscard]] std::vector<ui::UiAccessibilityNode>
  hudAccessibilitySnapshot() const;
  [[nodiscard]] std::uint64_t startHudTween(const ui::UiNodeHandle &node,
                                            const std::string &property,
                                            float target, float duration,
                                            std::string &error);
  [[nodiscard]] bool cancelHudTween(std::uint64_t handle);
  void setHudReducedMotion(bool enabled);
  [[nodiscard]] bool setHudLocale(const std::string &locale,
                                  std::string &error);
  void setHudPseudoLocale(bool enabled);
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
  [[nodiscard]] bool uiPointerCaptured(std::int64_t pointerId = 0) const;
  void addDebugLine(float x1, float y1, float x2, float y2, float r, float g,
                    float b, float a, float width = 1.0F);
  void clearDebugLines();
  [[nodiscard]] std::uint64_t playAudio(const std::string &assetId);
  [[nodiscard]] std::uint64_t playAudio(const AudioPlaybackRequest &request);
  [[nodiscard]] std::uint64_t playAudioSource(const std::string &entityId);
  [[nodiscard]] bool stopAudioSource(const std::string &entityId);
  [[nodiscard]] bool stopAudio(std::uint64_t handle);
  void setMasterVolume(float volume);
  [[nodiscard]] float masterVolume() const;
  [[nodiscard]] bool setAudioBusVolume(const std::string &bus, float volume);
  [[nodiscard]] float audioBusVolume(const std::string &bus) const;
  [[nodiscard]] bool setAudioBusMuted(const std::string &bus, bool muted);
  [[nodiscard]] bool setAudioBusPaused(const std::string &bus, bool paused);
  void
  defineAudioSnapshot(const std::string &name,
                      const std::unordered_map<std::string, float> &volumes);
  [[nodiscard]] bool transitionAudioSnapshot(const std::string &name,
                                             float duration);
  [[nodiscard]] std::uint64_t crossfadeAudio(std::uint64_t fromHandle,
                                             const std::string &assetId,
                                             const std::string &bus,
                                             float duration, bool loop,
                                             bool streaming);
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
  [[nodiscard]] platform::ApplicationServices &applicationServices();
  [[nodiscard]] const platform::ApplicationServices &
  applicationServices() const;
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
  void setHotReloadEnabled(bool enabled);
  [[nodiscard]] bool hotReloadEnabled() const;
  void beginFrame(float unscaledDeltaTime);
  void advanceFixedTime(float fixedDeltaTime);
  void setPaused(bool paused);
  [[nodiscard]] bool paused() const;
  void setTimeScale(float scale);
  [[nodiscard]] float timeScale() const;
  [[nodiscard]] float deltaTime() const;
  [[nodiscard]] float unscaledDeltaTime() const;
  [[nodiscard]] double gameTime() const;
  [[nodiscard]] double fixedTime() const;
  [[nodiscard]] std::uint64_t frameCount() const;
  void setApplicationFocused(bool focused);
  [[nodiscard]] bool applicationFocused() const;
  void setApplicationMinimized(bool minimized);
  void setApplicationSuspended(bool suspended);
  void notifyApplicationLowMemory();
  [[nodiscard]] bool applicationSuspended() const;
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
  [[nodiscard]] std::vector<std::string> publicLuaApi() const;

private:
  struct ScriptInstance {
    std::string entityId;
    std::string module;
    std::filesystem::path path;
    std::filesystem::file_time_type lastWriteTime{};
    int tableRef = 0;
    bool lifecycleStarted = false;
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
  [[nodiscard]] bool loadScriptInstance(std::string entityId,
                                        const std::string &module,
                                        const char *context,
                                        std::string &error);
  [[nodiscard]] bool loadDynamicEntityScript(const std::string &entityId,
                                             std::string &error);
  [[nodiscard]] bool loadDynamicUiScript(const std::string &uiId,
                                         std::string &error);
  void startScriptInstance(ScriptInstance &script);
  void unloadEntityScript(const std::string &entityId);
  void clearTimersAndEvents();
  void clearSaveMigrationHooks();
  void flushWorldCommands();
  [[nodiscard]] std::unordered_map<std::string, SaveValue> &
  loadSaveSlot(const std::string &slot);
  [[nodiscard]] bool writeSaveSlot(const std::string &slot);

  void *state_ = nullptr;
  World *world_ = nullptr;
  const ProjectData *project_ = nullptr;
  InputState *input_ = nullptr;
  input::InputActionMap inputActions_;
  std::unordered_set<std::string> activeInputContexts_;
  input::TouchGestureRecognizer touchGestureRecognizer_;
  std::vector<input::GestureEvent> gestureEvents_;
  platform::ApplicationServices applicationServices_;
  simulation::DeterministicRandom random_;
  isometric::IsoGridApi isoGridApi_;
  navigation::NavigationGrid2D navigationGrid2D_;
  TilemapRuntime tilemapRuntime_;
  DataAssetStore dataAssetStore_;
  const demi::AssetRegistry *assetRegistry_ = nullptr;
  std::optional<NetworkContract> networkContract_;
  AudioSystem *audio_ = nullptr;
  MediaSystem *media_ = nullptr;
  NetworkSystem *network_ = nullptr;
  RuntimeAssetService *runtimeAssets_ = nullptr;
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
  bool paused_ = false;
  float timeScale_ = 1.0F;
  float deltaTime_ = 0.0F;
  float unscaledDeltaTime_ = 0.0F;
  double gameTime_ = 0.0;
  double fixedTime_ = 0.0;
  std::uint64_t frameCount_ = 0;
  bool applicationFocused_ = true;
  bool applicationMinimized_ = false;
  bool applicationSuspended_ = false;
  bool hotReloadEnabled_ = false;
  bool cutscenePaused_ = false;
  bool previousUiMouseDown_ = false;
  std::optional<std::string> pendingSceneUnload_;
  std::uint64_t preparedSceneAssetRequest_ = 0;
  std::string preparedSceneAssetId_;
  std::string sceneAssetError_;
  std::unordered_set<std::string> activeSceneAssetGroups_;
  bool pendingPreparedActivation_ = false;
  bool autoActivatePrepared_ = false;
  std::string activeCutscene_;
  std::unordered_map<std::string, std::unordered_map<std::string, SaveValue>>
      saves_;
  std::vector<ScriptInstance> scripts_;
  std::vector<ModuleActionHandler> moduleActionHandlers_;
  std::vector<TimerInstance> timers_;
  std::vector<EventSubscription> eventSubscriptions_;
  std::vector<SaveMigrationHook> saveMigrationHooks_;
  WorldCommandBuffer worldCommands_;
  RuntimePrefabService prefabService_;
  SceneFlow sceneFlow_;
  ResourceLifetimeRegistry resourceLifetimes_;
  std::string lastSaveError_;
  std::uint64_t nextTimerId_ = 1;
  std::uint64_t nextMeshRevision_ = 1;
  std::uint64_t nextEventSubscriptionId_ = 1;
};

} // namespace demi::runtime
