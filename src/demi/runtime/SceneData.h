#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace demi::runtime {

struct Vec2 {
  float x = 0.0F;
  float y = 0.0F;
};

struct Color {
  float r = 0.08F;
  float g = 0.09F;
  float b = 0.12F;
  float a = 1.0F;
};

struct InputState {
  std::unordered_set<std::string> keysDown;
  std::unordered_set<std::string> mouseButtonsDown;
  Vec2 mousePosition;
};

struct DebugLine {
  Vec2 start;
  Vec2 end;
  Color color = {1.0F, 1.0F, 1.0F, 1.0F};
};

struct SceneEntry {
  std::string id;
  std::filesystem::path path;
};

struct ProjectData {
  std::filesystem::path projectPath;
  std::filesystem::path projectDirectory;
  std::string name;
  std::string mainScene;
  std::string scriptEntry;
  std::vector<SceneEntry> scenes;
};

struct HudButtonElement {
  std::string id;
  std::string group;
  std::string label;
  Vec2 position;
  Vec2 size = {120.0F, 40.0F};
  float scale = 3.0F;
  Color color = {0.16F, 0.18F, 0.32F, 1.0F};
  Color hoverColor = {0.24F, 0.28F, 0.48F, 1.0F};
  Color textColor = {1.0F, 1.0F, 1.0F, 1.0F};
  std::string script;
  bool visible = true;
  bool hovered = false;
};

struct HudTextElement {
  std::string id;
  std::string group;
  std::string text;
  Vec2 position;
  float scale = 3.0F;
  Color color = {1.0F, 1.0F, 1.0F, 1.0F};
  bool visible = true;
};

struct HudRectElement {
  std::string id;
  std::string group;
  Vec2 position;
  Vec2 size;
  Color color = {1.0F, 1.0F, 1.0F, 1.0F};
  bool visible = true;
};

struct Transform2DComponent {
  Vec2 position;
  float rotation = 0.0F;
  Vec2 scale = {1.0F, 1.0F};
};

struct Camera2DComponent {
  Color clearColor;
  float orthographicSize = 10.0F;
};

struct SpriteComponent {
  std::string texture;
  std::string layer;
};

struct IsoGridComponent {
  Vec2 cellSize = {1.0F, 0.5F};
  int width = 0;
  int height = 0;
};

struct IsoTransformComponent {
  Vec2 tile;
  float height = 0.0F;
  Vec2 footprint = {1.0F, 1.0F};
};

struct HitboxControllerComponent {
  Vec2 hurtbox = {1.0F, 1.0F};
  std::string script;
};

struct LuaScriptComponent {
  std::string module;
  float speed = 5.0F;
  float jumpSpeed = 8.0F;
};

struct BuildableComponent {
  std::string asset;
  bool blocksMovement = false;
};

struct Rigidbody2DComponent {
  std::string bodyType = "dynamic";
  Vec2 velocity;
  float gravityScale = 1.0F;
  float bounciness = 0.0F;
  bool lockRotation = true;
};

struct BoxCollider2DComponent {
  Vec2 size = {1.0F, 1.0F};
  Vec2 offset;
  bool isTrigger = false;
};

struct Entity {
  std::string id;
  std::string name;
  std::optional<Transform2DComponent> transform2D;
  std::optional<Camera2DComponent> camera2D;
  std::optional<SpriteComponent> sprite;
  std::optional<IsoGridComponent> isoGrid;
  std::optional<IsoTransformComponent> isoTransform;
  std::optional<HitboxControllerComponent> hitboxController;
  std::optional<LuaScriptComponent> luaScript;
  std::optional<BuildableComponent> buildable;
  std::optional<Rigidbody2DComponent> rigidbody2D;
  std::optional<BoxCollider2DComponent> boxCollider2D;
};

struct World {
  std::filesystem::path scenePath;
  std::string id;
  std::string name;
  std::vector<Entity> entities;
  std::vector<HudRectElement> hudRects;
  std::vector<HudButtonElement> hudButtons;
  std::vector<HudTextElement> hudText;
  std::vector<DebugLine> debugLines;
};

[[nodiscard]] inline Entity* findEntity(World& world, const std::string& id) {
  for (Entity& entity : world.entities) {
    if (entity.id == id) {
      return &entity;
    }
  }
  return nullptr;
}

[[nodiscard]] inline const Entity* findEntity(const World& world, const std::string& id) {
  for (const Entity& entity : world.entities) {
    if (entity.id == id) {
      return &entity;
    }
  }
  return nullptr;
}

[[nodiscard]] inline Entity* findFirstControllableEntity(World& world) {
  if (Entity* player = findEntity(world, "ent_player")) {
    return player;
  }

  for (Entity& entity : world.entities) {
    if (entity.transform2D.has_value() && entity.sprite.has_value()) {
      return &entity;
    }
  }
  return nullptr;
}

[[nodiscard]] inline const Camera2DComponent* activeCamera(const World& world) {
  for (const Entity& entity : world.entities) {
    if (entity.camera2D.has_value()) {
      return &*entity.camera2D;
    }
  }
  return nullptr;
}

[[nodiscard]] inline Vec2 activeCameraPosition(const World& world) {
  for (const Entity& entity : world.entities) {
    if (entity.camera2D.has_value() && entity.transform2D.has_value()) {
      return entity.transform2D->position;
    }
  }
  return {};
}

[[nodiscard]] inline std::size_t renderableEntityCount(const World& world) {
  std::size_t count = 0;
  for (const Entity& entity : world.entities) {
    if (entity.sprite.has_value() || entity.hitboxController.has_value() || entity.isoGrid.has_value() || entity.buildable.has_value() || entity.boxCollider2D.has_value()) {
      ++count;
    }
  }
  return count;
};

struct LoadedProject {
  ProjectData project;
  World world;
};

[[nodiscard]] std::optional<LoadedProject> loadProject(const std::filesystem::path& projectPath, std::string& error);

} // namespace demi::runtime
