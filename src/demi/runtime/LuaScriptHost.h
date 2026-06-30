#pragma once

#include "demi/runtime/SceneData.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

class AudioSystem;

class LuaScriptHost {
public:
  struct SaveValue {
    std::string value;
    bool number = false;
  };

  LuaScriptHost();
  ~LuaScriptHost();

  LuaScriptHost(const LuaScriptHost&) = delete;
  LuaScriptHost& operator=(const LuaScriptHost&) = delete;

  [[nodiscard]] bool initialize(World& world, const InputState& input, AudioSystem* audio, std::string& error);
  [[nodiscard]] bool loadWorldScripts(const ProjectData& project, World& world, std::string& error);
  void requestSceneLoad(const std::string& sceneId);
  [[nodiscard]] bool hasPendingSceneLoad() const;
  [[nodiscard]] bool applyPendingSceneLoad(std::string& error);
  [[nodiscard]] bool isKeyDown(const std::string& key) const;
  [[nodiscard]] bool addEntityPosition(const std::string& entityId, float dx, float dy);
  [[nodiscard]] bool setEntityPosition(const std::string& entityId, float x, float y);
  [[nodiscard]] std::optional<Vec2> entityPosition(const std::string& entityId) const;
  [[nodiscard]] std::optional<std::string> findEntityId(const std::string& idOrName) const;
  [[nodiscard]] bool destroyEntity(const std::string& entityId);
  [[nodiscard]] std::optional<Vec2> getRigidbodyVelocity(const std::string& entityId) const;
  [[nodiscard]] bool setRigidbodyVelocity(const std::string& entityId, float x, float y);
  [[nodiscard]] bool setRigidbodyVelocityX(const std::string& entityId, float x);
  [[nodiscard]] bool setRigidbodyVelocityY(const std::string& entityId, float y);
  [[nodiscard]] bool addRigidbodyImpulse(const std::string& entityId, float x, float y);
  [[nodiscard]] bool physicsOverlapBox(float x, float y, float width, float height, const std::string& ignoredEntityId) const;
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
  [[nodiscard]] bool isMouseDown(const std::string& button) const;
  [[nodiscard]] Vec2 mousePosition() const;
  [[nodiscard]] Vec2 mouseWorldPosition() const;
  [[nodiscard]] Vec2 viewportSize() const;
  void addDebugLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a);
  void clearDebugLines();
  [[nodiscard]] std::uint64_t playAudio(const std::string& assetId);
  [[nodiscard]] bool stopAudio(std::uint64_t handle);
  void setMasterVolume(float volume);
  [[nodiscard]] float masterVolume() const;
  void setViewport(int width, int height);
  void requestQuit();
  [[nodiscard]] bool quitRequested() const;
  void setWindowMode(std::string mode);
  [[nodiscard]] const std::string& windowMode() const;
  [[nodiscard]] bool windowModeDirty() const;
  void clearWindowModeDirty();
  void setPhysicsEnabled(bool enabled);
  [[nodiscard]] bool physicsEnabled() const;
  void start();
  void update(float dt);
  void fixedUpdate(float dt);
  void destroy();

private:
  struct ScriptInstance {
    std::string entityId;
    std::string module;
    int tableRef = 0;
  };

  void dispatchHudEvents();
  void unloadScripts();
  [[nodiscard]] std::unordered_map<std::string, SaveValue>& loadSaveSlot(const std::string& slot);
  [[nodiscard]] bool writeSaveSlot(const std::string& slot);

  void* state_ = nullptr;
  World* world_ = nullptr;
  const ProjectData* project_ = nullptr;
  const InputState* input_ = nullptr;
  AudioSystem* audio_ = nullptr;
  std::filesystem::path projectDirectory_;
  int viewportWidth_ = 1;
  int viewportHeight_ = 1;
  bool quitRequested_ = false;
  std::string windowMode_ = "windowed";
  bool windowModeDirty_ = false;
  bool physicsEnabled_ = true;
  bool previousUiMouseDown_ = false;
  std::optional<std::string> pendingSceneLoad_;
  std::unordered_map<std::string, std::unordered_map<std::string, SaveValue>> saves_;
  std::vector<ScriptInstance> scripts_;
};

} // namespace demi::runtime
