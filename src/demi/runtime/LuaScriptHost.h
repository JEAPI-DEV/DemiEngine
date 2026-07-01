#pragma once

#include "demi/runtime/SceneData.h"
#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/Physics2D.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

class AudioSystem;
class MediaSystem;

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

  LuaScriptHost(const LuaScriptHost&) = delete;
  LuaScriptHost& operator=(const LuaScriptHost&) = delete;

  [[nodiscard]] bool initialize(World& world, const InputState& input, AudioSystem* audio, std::string& error);
  void setMediaSystem(MediaSystem* media);
  [[nodiscard]] bool loadWorldScripts(const ProjectData& project, World& world, std::string& error);
  void requestSceneLoad(const std::string& sceneId);
  [[nodiscard]] bool hasPendingSceneLoad() const;
  [[nodiscard]] bool applyPendingSceneLoad(std::string& error);
  [[nodiscard]] bool isKeyDown(const std::string& key) const;
  [[nodiscard]] bool addEntityPosition(const std::string& entityId, float dx, float dy);
  [[nodiscard]] bool setEntityPosition(const std::string& entityId, float x, float y);
  [[nodiscard]] std::optional<Vec2> entityPosition(const std::string& entityId) const;
  [[nodiscard]] std::optional<float> entityRotation(const std::string& entityId) const;
  [[nodiscard]] bool setEntityRotation(const std::string& entityId, float rotation);
  [[nodiscard]] std::optional<Vec2> entityScale(const std::string& entityId) const;
  [[nodiscard]] bool setEntityScale(const std::string& entityId, float x, float y);
  [[nodiscard]] std::optional<std::string> findEntityId(const std::string& idOrName) const;
  [[nodiscard]] bool destroyEntity(const std::string& entityId);
  [[nodiscard]] std::optional<Vec2> getRigidbodyVelocity(const std::string& entityId) const;
  [[nodiscard]] bool setRigidbodyVelocity(const std::string& entityId, float x, float y);
  [[nodiscard]] bool setRigidbodyVelocityX(const std::string& entityId, float x);
  [[nodiscard]] bool setRigidbodyVelocityY(const std::string& entityId, float y);
  [[nodiscard]] bool addRigidbodyImpulse(const std::string& entityId, float x, float y);
  [[nodiscard]] bool physicsOverlapBox(float x, float y, float width, float height, const std::string& ignoredEntityId) const;
  [[nodiscard]] bool physicsHasContact(const std::string& entityId, const PhysicsContactFilter2D& filter) const;
  [[nodiscard]] std::vector<PhysicsContact2D> physicsContacts(const std::string& entityId) const;
  [[nodiscard]] bool createEntity(Entity entity);
  [[nodiscard]] bool setHudText(const std::string& id, const std::string& text);
  [[nodiscard]] bool createHudText(const std::string& id, const std::string& text, float x, float y, float scale, Color color);
  [[nodiscard]] bool createHudRect(const std::string& id, float x, float y, float width, float height, Color color);
  [[nodiscard]] bool setHudRect(const std::string& id, float x, float y, float width, float height);
  [[nodiscard]] bool setHudColor(const std::string& id, Color color);
  [[nodiscard]] bool setHudVisible(const std::string& id, bool visible);
  [[nodiscard]] bool setHudGroupVisible(const std::string& group, bool visible);
  [[nodiscard]] std::optional<std::string> hudText(const std::string& id) const;
  [[nodiscard]] std::optional<float> saveNumber(const std::string& slot, const std::string& key);
  [[nodiscard]] std::optional<std::string> saveString(const std::string& slot, const std::string& key);
  [[nodiscard]] bool setSaveNumber(const std::string& slot, const std::string& key, float value);
  [[nodiscard]] bool setSaveString(const std::string& slot, const std::string& key, const std::string& value);
  [[nodiscard]] bool saveExists(const std::string& slot) const;
  [[nodiscard]] bool deleteSave(const std::string& slot);
  [[nodiscard]] std::optional<std::string> readSaveDocument(const std::string& slot) const;
  [[nodiscard]] bool writeSaveDocument(const std::string& slot, const std::string& stateJson, int formatVersion);
  [[nodiscard]] int saveFormatVersion(const std::string& slot) const;
  void addSaveMigrationHook(int fromVersion, int toVersion, int callbackRef);
  [[nodiscard]] const std::vector<SaveMigrationHook>& saveMigrationHooks() const;
  [[nodiscard]] bool isMouseDown(const std::string& button) const;
  [[nodiscard]] Vec2 mousePosition() const;
  [[nodiscard]] Vec2 mouseWorldPosition() const;
  [[nodiscard]] Vec2 viewportSize() const;
  void addDebugLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a);
  void clearDebugLines();
  [[nodiscard]] std::uint64_t playAudio(const std::string& assetId);
  [[nodiscard]] std::uint64_t playAudioSource(const std::string& entityId);
  [[nodiscard]] bool stopAudioSource(const std::string& entityId);
  [[nodiscard]] bool stopAudio(std::uint64_t handle);
  void setMasterVolume(float volume);
  [[nodiscard]] float masterVolume() const;
  [[nodiscard]] std::uint64_t playVideo(const std::string& assetId, bool loop);
  [[nodiscard]] std::uint64_t playVideoPlayer(const std::string& entityId);
  [[nodiscard]] bool stopVideo(std::uint64_t handle);
  [[nodiscard]] bool isVideoPlaying(std::uint64_t handle) const;
  [[nodiscard]] bool startCutscene(std::string id);
  [[nodiscard]] bool pauseCutscene();
  [[nodiscard]] bool resumeCutscene();
  [[nodiscard]] bool stopCutscene();
  [[nodiscard]] bool isCutscenePlaying() const;
  [[nodiscard]] const std::string& activeCutscene() const;
  void setViewport(int width, int height);
  void requestQuit();
  [[nodiscard]] bool quitRequested() const;
  void setWindowMode(std::string mode);
  [[nodiscard]] const std::string& windowMode() const;
  [[nodiscard]] bool windowModeDirty() const;
  void clearWindowModeDirty();
  void setPhysicsEnabled(bool enabled);
  [[nodiscard]] bool physicsEnabled() const;
  [[nodiscard]] std::uint64_t addTimer(float seconds, bool repeating, int callbackRef);
  [[nodiscard]] bool cancelTimer(std::uint64_t timerId);
  [[nodiscard]] std::uint64_t addEventSubscription(std::string eventName, int callbackRef);
  [[nodiscard]] bool removeEventSubscription(std::uint64_t subscriptionId);
  [[nodiscard]] int emitEvent(const std::string& eventName, int payloadIndex);
  void start();
  void update(float dt);
  void fixedUpdate(float dt);
  void destroy();

  [[nodiscard]] static Diagnostics checkScriptSyntax(const std::filesystem::path& path);

private:
  struct ScriptInstance {
    std::string entityId;
    std::string module;
    std::filesystem::path path;
    std::filesystem::file_time_type lastWriteTime{};
    int tableRef = 0;
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

  void dispatchHudEvents();
  void updateTimers(float dt);
  void reloadChangedScripts();
  void unloadScripts();
  void clearTimersAndEvents();
  [[nodiscard]] std::unordered_map<std::string, SaveValue>& loadSaveSlot(const std::string& slot);
  [[nodiscard]] bool writeSaveSlot(const std::string& slot);

  void* state_ = nullptr;
  World* world_ = nullptr;
  const ProjectData* project_ = nullptr;
  const InputState* input_ = nullptr;
  AudioSystem* audio_ = nullptr;
  MediaSystem* media_ = nullptr;
  std::filesystem::path projectDirectory_;
  int viewportWidth_ = 1;
  int viewportHeight_ = 1;
  bool quitRequested_ = false;
  std::string windowMode_ = "windowed";
  bool windowModeDirty_ = false;
  bool physicsEnabled_ = true;
  bool cutscenePaused_ = false;
  bool previousUiMouseDown_ = false;
  std::optional<std::string> pendingSceneLoad_;
  std::string activeCutscene_;
  std::unordered_map<std::string, std::unordered_map<std::string, SaveValue>> saves_;
  std::vector<ScriptInstance> scripts_;
  std::vector<TimerInstance> timers_;
  std::vector<EventSubscription> eventSubscriptions_;
  std::vector<SaveMigrationHook> saveMigrationHooks_;
  std::uint64_t nextTimerId_ = 1;
  std::uint64_t nextEventSubscriptionId_ = 1;
};

} // namespace demi::runtime
