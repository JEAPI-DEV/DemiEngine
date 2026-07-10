#pragma once

#include <array>
#include <cmath>
#include <cstdint>
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

struct Vec3 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

struct Color {
  float r = 0.08F;
  float g = 0.09F;
  float b = 0.12F;
  float a = 1.0F;
};

struct InputState {
  std::unordered_set<std::string> keysDown;
  std::unordered_set<std::string> keysPressed;
  std::unordered_set<std::string> mouseButtonsDown;
  Vec2 mousePosition;
  Vec2 mouseDelta;
  std::string textEntered;
};

struct LuaActionHandler {
  std::string action;
  std::string functionName;
};

struct LuaEventHandler {
  std::string eventName;
  std::string functionName;
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
  std::vector<std::string> scriptModules;
  std::vector<SceneEntry> scenes;
};

struct HudButtonElement {
  std::string id;
  std::string group;
  std::string label;
  Vec2 position;
  Vec2 size = {120.0F, 40.0F};
  float scale = 3.0F;
  float fontSize = 0.0F;
  Color color = {0.16F, 0.18F, 0.32F, 1.0F};
  Color hoverColor = {0.24F, 0.28F, 0.48F, 1.0F};
  Color textColor = {1.0F, 1.0F, 1.0F, 1.0F};
  std::string script;
  std::string action;
  bool visible = true;
  bool hovered = false;
};

struct HudTextElement {
  std::string id;
  std::string group;
  std::string text;
  Vec2 position;
  float scale = 3.0F;
  float fontSize = 0.0F;
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

struct HudImageElement {
  std::string id;
  std::string group;
  std::string texture;
  std::string animation;
  int animationFrame = 0;
  Vec2 position;
  Vec2 size;
  Vec2 sourcePosition;
  Vec2 sourceSize;
  Color color = {1.0F, 1.0F, 1.0F, 1.0F};
  bool visible = true;
};

struct Transform2DComponent {
  std::string parent;
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
  std::string shape = "rectangle";
  std::string layer;
  Color color = {1.0F, 1.0F, 1.0F, 1.0F};
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
  std::string propertiesJson;
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
  std::string layer;
};

struct Transform3DComponent {
  std::string parent;
  Vec3 position;
  Vec3 rotation;
  Vec3 scale = {1.0F, 1.0F, 1.0F};
};

struct Camera3DComponent {
  Color clearColor;
  float fov = 60.0F;
  float orthographicSize = 10.0F;
  Vec3 targetOffset = {0.0F, 0.0F, 1.0F};
  bool perspective = true;
  float positionX = 0.0F;
  float upAxis = 1.0F;
};

struct MeshRendererComponent {
  std::string model;
  std::string shape = "cube";
  Vec3 size = {1.0F, 1.0F, 1.0F};
  Color color = {0.8F, 0.8F, 0.8F, 1.0F};
  std::string texture;
  std::vector<Vec3> vertices;
  std::vector<Vec3> normals;
  std::vector<Vec2> uvs;
  std::uint64_t revision = 0;
  Vec3 boundsMin;
  Vec3 boundsMax;
  bool hasBounds = false;
  bool wireframe = false;
};

struct BoxCollider3DComponent {
  Vec3 size = {1.0F, 1.0F, 1.0F};
  Vec3 offset;
  bool isTrigger = false;
  std::string layer;
};

struct SphereCollider3DComponent {
  float radius = 0.5F;
  Vec3 offset;
  bool isTrigger = false;
  std::string layer;
};

struct Rigidbody3DComponent {
  std::string bodyType = "dynamic";
  Vec3 velocity;
  bool useGravity = true;
  float gravityScale = 1.0F;
};

struct DirectionalLightComponent {
  Vec3 direction = {-0.4F, -1.0F, -0.3F};
  Color color = {1.0F, 1.0F, 0.95F, 1.0F};
  float intensity = 1.0F;
};

struct PhysicsContact2D {
  std::string entityId;
  std::string otherEntityId;
  std::string otherLayer;
  Vec2 normal;
  bool isTrigger = false;
};

struct AudioSourceComponent {
  std::string clip;
  bool playOnStart = false;
  bool loop = false;
  float volume = 1.0F;
  std::uint64_t handle = 0;
};

struct AudioListenerComponent {
  bool primary = true;
};

struct VideoPlayerComponent {
  std::string clip;
  bool playOnStart = false;
  bool loop = false;
  std::uint64_t handle = 0;
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
  std::optional<Transform3DComponent> transform3D;
  std::optional<Camera3DComponent> camera3D;
  std::optional<MeshRendererComponent> meshRenderer;
  std::optional<BoxCollider3DComponent> boxCollider3D;
  std::optional<SphereCollider3DComponent> sphereCollider3D;
  std::optional<Rigidbody3DComponent> rigidbody3D;
  std::optional<DirectionalLightComponent> directionalLight;
  std::optional<AudioSourceComponent> audioSource;
  std::optional<AudioListenerComponent> audioListener;
  std::optional<VideoPlayerComponent> videoPlayer;
};

struct World {
  std::filesystem::path scenePath;
  std::string id;
  std::string name;
  Vec2 hudCanvasSize = {960.0F, 540.0F};
  std::vector<Entity> entities;
  std::vector<HudRectElement> hudRects;
  std::vector<HudImageElement> hudImages;
  std::vector<HudButtonElement> hudButtons;
  std::vector<HudTextElement> hudText;
  std::vector<DebugLine> debugLines;
  std::vector<PhysicsContact2D> physicsContacts;
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

[[nodiscard]] inline Vec2 rotate2D(const Vec2 value, const float rotation) {
  const float sinR = std::sin(rotation);
  const float cosR = std::cos(rotation);
  return Vec2{.x = value.x * cosR - value.y * sinR, .y = value.x * sinR + value.y * cosR};
}

[[nodiscard]] inline Vec2 worldPosition2D(const World& world, const Entity& entity) {
  if (!entity.transform2D.has_value()) {
    return {};
  }
  Vec2 position = entity.transform2D->position;
  if (!entity.transform2D->parent.empty()) {
    if (const Entity* parent = findEntity(world, entity.transform2D->parent); parent != nullptr && parent->transform2D.has_value()) {
      const Vec2 rotated = rotate2D(position, parent->transform2D->rotation);
      const Vec2 parentPosition = worldPosition2D(world, *parent);
      position = Vec2{.x = parentPosition.x + rotated.x, .y = parentPosition.y + rotated.y};
    }
  }
  return position;
}

[[nodiscard]] inline float worldRotation2D(const World& world, const Entity& entity) {
  if (!entity.transform2D.has_value()) {
    return 0.0F;
  }
  float rotation = entity.transform2D->rotation;
  if (!entity.transform2D->parent.empty()) {
    if (const Entity* parent = findEntity(world, entity.transform2D->parent); parent != nullptr && parent->transform2D.has_value()) {
      rotation += worldRotation2D(world, *parent);
    }
  }
  return rotation;
}

[[nodiscard]] inline Vec2 activeCameraPosition(const World& world) {
  for (const Entity& entity : world.entities) {
    if (entity.camera2D.has_value() && entity.transform2D.has_value()) {
      return worldPosition2D(world, entity);
    }
  }
  return {};
}

[[nodiscard]] inline const Camera3DComponent* activeCamera3D(const World& world) {
  for (const Entity& entity : world.entities) {
    if (entity.camera3D.has_value()) {
      return &*entity.camera3D;
    }
  }
  return nullptr;
}

[[nodiscard]] inline Vec3 rotateYaw(const Vec3 value, const float yaw) {
  const float sinY = std::sin(yaw);
  const float cosY = std::cos(yaw);
  return Vec3{
    .x = value.x * cosY + value.z * sinY,
    .y = value.y,
    .z = -value.x * sinY + value.z * cosY,
  };
}

[[nodiscard]] inline Vec3 worldPosition3D(const World& world, const Entity& entity) {
  if (!entity.transform3D.has_value()) {
    return {};
  }
  Vec3 position = entity.transform3D->position;
  if (!entity.transform3D->parent.empty()) {
    if (const Entity* parent = findEntity(world, entity.transform3D->parent); parent != nullptr && parent->transform3D.has_value()) {
      const Vec3 rotated = rotateYaw(position, parent->transform3D->rotation.y);
      const Vec3 parentPosition = worldPosition3D(world, *parent);
      position = Vec3{.x = parentPosition.x + rotated.x, .y = parentPosition.y + rotated.y, .z = parentPosition.z + rotated.z};
    }
  }
  return position;
}

[[nodiscard]] inline Vec3 worldRotation3D(const World& world, const Entity& entity) {
  if (!entity.transform3D.has_value()) {
    return {};
  }
  Vec3 rotation = entity.transform3D->rotation;
  if (!entity.transform3D->parent.empty()) {
    if (const Entity* parent = findEntity(world, entity.transform3D->parent); parent != nullptr && parent->transform3D.has_value()) {
      const Vec3 parentRotation = worldRotation3D(world, *parent);
      rotation.x += parentRotation.x;
      rotation.y += parentRotation.y;
      rotation.z += parentRotation.z;
    }
  }
  return rotation;
}

[[nodiscard]] inline Vec3 activeCamera3DPosition(const World& world) {
  for (const Entity& entity : world.entities) {
    if (entity.camera3D.has_value() && entity.transform3D.has_value()) {
      return worldPosition3D(world, entity);
    }
  }
  return {};
}

[[nodiscard]] inline Vec3 activeCamera3DRotation(const World& world) {
  for (const Entity& entity : world.entities) {
    if (entity.camera3D.has_value() && entity.transform3D.has_value()) {
      return worldRotation3D(world, entity);
    }
  }
  return {};
}

[[nodiscard]] inline bool sceneIs3D(const World& world) {
  return activeCamera3D(world) != nullptr;
}

[[nodiscard]] inline std::size_t renderableEntityCount(const World& world) {
  std::size_t count = 0;
  for (const Entity& entity : world.entities) {
    if (entity.sprite.has_value() || entity.hitboxController.has_value() || entity.isoGrid.has_value() || entity.buildable.has_value() || entity.boxCollider2D.has_value() || entity.videoPlayer.has_value() || entity.meshRenderer.has_value() || entity.boxCollider3D.has_value() || entity.sphereCollider3D.has_value()) {
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

// Loads a single scene (and its optional HUD) by scene id from a project's scene
// list. Used at runtime to switch scenes without reloading the whole project.
[[nodiscard]] std::optional<World> loadScene(const ProjectData& project, const std::string& sceneId, std::string& error);

} // namespace demi::runtime
