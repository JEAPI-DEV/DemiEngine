#pragma once

#include "demi/runtime/SceneData.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {

class LuaScriptHost {
public:
  LuaScriptHost();
  ~LuaScriptHost();

  LuaScriptHost(const LuaScriptHost&) = delete;
  LuaScriptHost& operator=(const LuaScriptHost&) = delete;

  [[nodiscard]] bool initialize(World& world, const InputState& input, std::string& error);
  [[nodiscard]] bool loadWorldScripts(const ProjectData& project, World& world, std::string& error);
  [[nodiscard]] bool isKeyDown(const std::string& key) const;
  [[nodiscard]] bool addEntityPosition(const std::string& entityId, float dx, float dy);
  [[nodiscard]] bool setEntityPosition(const std::string& entityId, float x, float y);
  [[nodiscard]] std::optional<Vec2> entityPosition(const std::string& entityId) const;
  [[nodiscard]] std::optional<std::string> findEntityId(const std::string& idOrName) const;
  [[nodiscard]] std::optional<Vec2> getRigidbodyVelocity(const std::string& entityId) const;
  [[nodiscard]] bool setRigidbodyVelocity(const std::string& entityId, float x, float y);
  [[nodiscard]] bool setRigidbodyVelocityX(const std::string& entityId, float x);
  [[nodiscard]] bool setRigidbodyVelocityY(const std::string& entityId, float y);
  [[nodiscard]] bool addRigidbodyImpulse(const std::string& entityId, float x, float y);
  [[nodiscard]] bool physicsOverlapBox(float x, float y, float width, float height, const std::string& ignoredEntityId) const;
  [[nodiscard]] bool setHudText(const std::string& id, const std::string& text);
  [[nodiscard]] bool createHudText(const std::string& id, const std::string& text, float x, float y, float scale);
  [[nodiscard]] std::optional<std::string> hudText(const std::string& id) const;
  [[nodiscard]] bool isMouseDown(const std::string& button) const;
  [[nodiscard]] Vec2 mousePosition() const;
  [[nodiscard]] Vec2 mouseWorldPosition() const;
  void addDebugLine(float x1, float y1, float x2, float y2, float r, float g, float b, float a);
  void clearDebugLines();
  void setViewport(int width, int height);
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

  void* state_ = nullptr;
  World* world_ = nullptr;
  const InputState* input_ = nullptr;
  int viewportWidth_ = 1;
  int viewportHeight_ = 1;
  std::vector<ScriptInstance> scripts_;
};

} // namespace demi::runtime
