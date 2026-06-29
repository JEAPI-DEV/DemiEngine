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
  std::vector<ScriptInstance> scripts_;
};

} // namespace demi::runtime
