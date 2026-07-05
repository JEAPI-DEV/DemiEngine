#include "demi/runtime/scene/SceneEntityParser.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>

namespace demi::runtime::scene_loading {

namespace {

using ComponentParseFn = void (*)(const Json& componentJson, Entity& entity);

struct ComponentParser {
  std::string_view name;
  ComponentParseFn parse;
};

void parseTransform2D(const Json& json, Entity& entity) {
  Transform2DComponent component;
  component.parent = stringOr(json, "parent");
  if (const std::optional<Vec2> position = vec2Field(json, "position")) {
    component.position = *position;
  }
  if (const std::optional<float> rotation = numberField(json, "rotation")) {
    component.rotation = *rotation;
  }
  if (const std::optional<Vec2> scale = vec2Field(json, "scale")) {
    component.scale = *scale;
  }
  entity.transform2D = component;
}

void parseCamera2D(const Json& json, Entity& entity) {
  Camera2DComponent component;
  if (const std::optional<Color> clearColor = colorField(json, "clear_color")) {
    component.clearColor = *clearColor;
  }
  if (const std::optional<float> orthographicSize = numberField(json, "orthographic_size")) {
    component.orthographicSize = *orthographicSize;
  }
  entity.camera2D = component;
}

void parseSprite(const Json& json, Entity& entity) {
  SpriteComponent component;
  component.texture = stringOr(json, "texture");
  component.shape = stringOr(json, "shape", "rectangle");
  component.layer = stringOr(json, "layer");
  if (const std::optional<Color> color = colorField(json, "color")) {
    component.color = *color;
  }
  entity.sprite = component;
}

void parseIsoGrid(const Json& json, Entity& entity) {
  IsoGridComponent component;
  if (const std::optional<Vec2> cellSize = vec2Field(json, "cell_size")) {
    component.cellSize = *cellSize;
  }
  if (const std::optional<float> width = numberField(json, "width")) {
    component.width = static_cast<int>(*width);
  }
  if (const std::optional<float> height = numberField(json, "height")) {
    component.height = static_cast<int>(*height);
  }
  entity.isoGrid = component;
}

void parseIsoTransform(const Json& json, Entity& entity) {
  IsoTransformComponent component;
  if (const std::optional<Vec2> tile = vec2Field(json, "tile")) {
    component.tile = *tile;
  }
  if (const std::optional<float> height = numberField(json, "height")) {
    component.height = *height;
  }
  if (const std::optional<Vec2> footprint = vec2Field(json, "footprint")) {
    component.footprint = *footprint;
  }
  entity.isoTransform = component;
}

void parseHitboxController(const Json& json, Entity& entity) {
  HitboxControllerComponent component;
  if (const std::optional<Vec2> hurtbox = vec2Field(json, "hurtbox")) {
    component.hurtbox = *hurtbox;
  }
  component.script = stringOr(json, "script");
  entity.hitboxController = component;
}

void parseLuaScript(const Json& json, Entity& entity) {
  LuaScriptComponent component;
  component.module = stringOr(json, "module");
  if (const Json* properties = objectField(json, "properties")) {
    component.propertiesJson = properties->dump();
  }
  entity.luaScript = component;
}

void parseBuildable(const Json& json, Entity& entity) {
  BuildableComponent component;
  component.asset = stringOr(json, "asset");
  component.blocksMovement = boolField(json, "blocks_movement").value_or(false);
  entity.buildable = component;
}

void parseRigidbody2D(const Json& json, Entity& entity) {
  Rigidbody2DComponent component;
  component.bodyType = stringOr(json, "body_type", "dynamic");
  if (const std::optional<Vec2> velocity = vec2Field(json, "velocity")) {
    component.velocity = *velocity;
  }
  if (const std::optional<float> gravityScale = numberField(json, "gravity_scale")) {
    component.gravityScale = *gravityScale;
  }
  if (const std::optional<float> bounciness = numberField(json, "bounciness")) {
    component.bounciness = *bounciness;
  }
  component.lockRotation = boolField(json, "lock_rotation").value_or(true);
  entity.rigidbody2D = component;
}

void parseBoxCollider2D(const Json& json, Entity& entity) {
  BoxCollider2DComponent component;
  if (const std::optional<Vec2> size = vec2Field(json, "size")) {
    component.size = *size;
  }
  if (const std::optional<Vec2> offset = vec2Field(json, "offset")) {
    component.offset = *offset;
  }
  component.isTrigger = boolField(json, "is_trigger").value_or(false);
  component.layer = stringOr(json, "layer");
  entity.boxCollider2D = component;
}

void parseTransform3D(const Json& json, Entity& entity) {
  Transform3DComponent component;
  component.parent = stringOr(json, "parent");
  if (const std::optional<Vec3> position = vec3Field(json, "position")) {
    component.position = *position;
  }
  if (const std::optional<Vec3> rotation = vec3Field(json, "rotation")) {
    component.rotation = *rotation;
  }
  if (const std::optional<Vec3> scale = vec3Field(json, "scale")) {
    component.scale = *scale;
  }
  entity.transform3D = component;
}

void parseCamera3D(const Json& json, Entity& entity) {
  Camera3DComponent component;
  if (const std::optional<Color> clearColor = colorField(json, "clear_color")) {
    component.clearColor = *clearColor;
  }
  if (const std::optional<float> fov = numberField(json, "fov")) {
    component.fov = *fov;
  }
  if (const std::optional<float> orthoSize = numberField(json, "orthographic_size")) {
    component.orthographicSize = *orthoSize;
  }
  if (const std::optional<Vec3> targetOffset = vec3Field(json, "target_offset")) {
    component.targetOffset = *targetOffset;
  }
  component.perspective = boolField(json, "perspective").value_or(true);
  if (const std::optional<float> positionX = numberField(json, "position_x")) {
    component.positionX = *positionX;
  }
  if (const std::optional<float> upAxis = numberField(json, "up_axis")) {
    component.upAxis = *upAxis;
  }
  entity.camera3D = component;
}

void parseMeshRenderer(const Json& json, Entity& entity) {
  MeshRendererComponent component;
  component.model = stringOr(json, "model");
  component.shape = stringOr(json, "shape", "cube");
  if (const std::optional<Vec3> size = vec3Field(json, "size")) {
    component.size = *size;
  }
  if (const std::optional<Color> color = colorField(json, "color")) {
    component.color = *color;
  }
  component.texture = stringOr(json, "texture");
  component.wireframe = boolField(json, "wireframe").value_or(false);
  if (const Json* vertices = arrayField(json, "vertices")) {
    component.vertices.reserve(vertices->size());
    for (const Json& vertexJson : *vertices) {
      if (!vertexJson.is_array() || vertexJson.size() < 3) {
        continue;
      }
      component.vertices.push_back(Vec3{vertexJson[0].get<float>(), vertexJson[1].get<float>(), vertexJson[2].get<float>()});
    }
  }
  if (const Json* normals = arrayField(json, "normals")) {
    component.normals.reserve(normals->size());
    for (const Json& normalJson : *normals) {
      if (!normalJson.is_array() || normalJson.size() < 3) {
        continue;
      }
      component.normals.push_back(Vec3{normalJson[0].get<float>(), normalJson[1].get<float>(), normalJson[2].get<float>()});
    }
  }
  if (const Json* uvs = arrayField(json, "uvs")) {
    component.uvs.reserve(uvs->size());
    for (const Json& uvJson : *uvs) {
      if (!uvJson.is_array() || uvJson.size() < 2) {
        continue;
      }
      component.uvs.push_back(Vec2{uvJson[0].get<float>(), uvJson[1].get<float>()});
    }
  }
  entity.meshRenderer = component;
}

void parseBoxCollider3D(const Json& json, Entity& entity) {
  BoxCollider3DComponent component;
  if (const std::optional<Vec3> size = vec3Field(json, "size")) {
    component.size = *size;
  }
  if (const std::optional<Vec3> offset = vec3Field(json, "offset")) {
    component.offset = *offset;
  }
  component.isTrigger = boolField(json, "is_trigger").value_or(false);
  component.layer = stringOr(json, "layer");
  entity.boxCollider3D = component;
}

void parseSphereCollider3D(const Json& json, Entity& entity) {
  SphereCollider3DComponent component;
  if (const std::optional<float> radius = numberField(json, "radius")) {
    component.radius = *radius;
  }
  if (const std::optional<Vec3> offset = vec3Field(json, "offset")) {
    component.offset = *offset;
  }
  component.isTrigger = boolField(json, "is_trigger").value_or(false);
  component.layer = stringOr(json, "layer");
  entity.sphereCollider3D = component;
}

void parseRigidbody3D(const Json& json, Entity& entity) {
  Rigidbody3DComponent component;
  component.bodyType = stringOr(json, "body_type", "dynamic");
  if (const std::optional<Vec3> velocity = vec3Field(json, "velocity")) {
    component.velocity = *velocity;
  }
  component.useGravity = boolField(json, "use_gravity").value_or(true);
  if (const std::optional<float> gravityScale = numberField(json, "gravity_scale")) {
    component.gravityScale = *gravityScale;
  }
  entity.rigidbody3D = component;
}

void parseDirectionalLight(const Json& json, Entity& entity) {
  DirectionalLightComponent component;
  if (const std::optional<Vec3> direction = vec3Field(json, "direction")) {
    component.direction = *direction;
  }
  if (const std::optional<Color> color = colorField(json, "color")) {
    component.color = *color;
  }
  if (const std::optional<float> intensity = numberField(json, "intensity")) {
    component.intensity = *intensity;
  }
  entity.directionalLight = component;
}

void parseAudioSource(const Json& json, Entity& entity) {
  AudioSourceComponent component;
  component.clip = stringOr(json, "clip");
  component.playOnStart = boolField(json, "play_on_start").value_or(false);
  component.loop = boolField(json, "loop").value_or(false);
  if (const std::optional<float> volume = numberField(json, "volume")) {
    component.volume = *volume;
  }
  entity.audioSource = component;
}

void parseAudioListener(const Json& json, Entity& entity) {
  AudioListenerComponent component;
  component.primary = boolField(json, "primary").value_or(true);
  entity.audioListener = component;
}

void parseVideoPlayer(const Json& json, Entity& entity) {
  VideoPlayerComponent component;
  component.clip = stringOr(json, "clip");
  component.playOnStart = boolField(json, "play_on_start").value_or(false);
  component.loop = boolField(json, "loop").value_or(false);
  entity.videoPlayer = component;
}

constexpr std::array componentParsers{
  ComponentParser{.name = "Transform2D", .parse = parseTransform2D},
  ComponentParser{.name = "Camera2D", .parse = parseCamera2D},
  ComponentParser{.name = "Sprite", .parse = parseSprite},
  ComponentParser{.name = "IsoGrid", .parse = parseIsoGrid},
  ComponentParser{.name = "IsoTransform", .parse = parseIsoTransform},
  ComponentParser{.name = "HitboxController", .parse = parseHitboxController},
  ComponentParser{.name = "LuaScript", .parse = parseLuaScript},
  ComponentParser{.name = "Buildable", .parse = parseBuildable},
  ComponentParser{.name = "Rigidbody2D", .parse = parseRigidbody2D},
  ComponentParser{.name = "BoxCollider2D", .parse = parseBoxCollider2D},
  ComponentParser{.name = "Transform3D", .parse = parseTransform3D},
  ComponentParser{.name = "Camera3D", .parse = parseCamera3D},
  ComponentParser{.name = "MeshRenderer", .parse = parseMeshRenderer},
  ComponentParser{.name = "BoxCollider3D", .parse = parseBoxCollider3D},
  ComponentParser{.name = "SphereCollider3D", .parse = parseSphereCollider3D},
  ComponentParser{.name = "Rigidbody3D", .parse = parseRigidbody3D},
  ComponentParser{.name = "DirectionalLight", .parse = parseDirectionalLight},
  ComponentParser{.name = "AudioSource", .parse = parseAudioSource},
  ComponentParser{.name = "AudioListener", .parse = parseAudioListener},
  ComponentParser{.name = "VideoPlayer", .parse = parseVideoPlayer},
};

void parseComponents(const Json& entityJson, Entity& entity) {
  const Json* components = objectField(entityJson, "components");
  const Json& componentSource = components != nullptr ? *components : entityJson;
  for (const ComponentParser& parser : componentParsers) {
    if (const Json* componentJson = objectField(componentSource, parser.name.data())) {
      parser.parse(*componentJson, entity);
    }
  }
}

Entity parseEntity(const Json& entityJson) {
  Entity entity;
  entity.id = stringOr(entityJson, "id", "ent_unknown");
  entity.name = stringOr(entityJson, "name", entity.id);
  parseComponents(entityJson, entity);
  return entity;
}

} // namespace

World parseSceneWorld(const std::filesystem::path& scenePath, const Json& document) {
  World world;
  world.scenePath = scenePath;
  world.id = stringOr(document, "id", "scene://unknown");
  world.name = stringOr(document, "name", world.id);

  const Json* entities = arrayField(document, "entities");
  if (entities == nullptr) {
    return world;
  }

  for (const Json& entityJson : *entities) {
    if (entityJson.is_object()) {
      world.entities.push_back(parseEntity(entityJson));
    }
  }
  return world;
}

} // namespace demi::runtime::scene_loading
