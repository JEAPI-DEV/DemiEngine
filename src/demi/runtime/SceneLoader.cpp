#include "demi/runtime/SceneData.h"

#include "demi/diagnostics/Diagnostic.h"
#include "demi/schema/Validation.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string_view>

namespace demi::runtime {

namespace {

std::string readFile(const std::filesystem::path& path, std::string& error) {
  std::ifstream input(path);
  if (!input) {
    error = "Failed to read file: " + path.string();
    return {};
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::optional<std::size_t> colonAfterKey(const std::string& text, std::string_view key, std::size_t start = 0) {
  const std::string quotedKey = "\"" + std::string(key) + "\"";
  std::size_t cursor = start;

  while (true) {
    const std::size_t keyPos = text.find(quotedKey, cursor);
    if (keyPos == std::string::npos) {
      return std::nullopt;
    }

    std::size_t colon = keyPos + quotedKey.size();
    while (colon < text.size() && std::isspace(static_cast<unsigned char>(text[colon]))) {
      ++colon;
    }

    if (colon < text.size() && text[colon] == ':') {
      return colon;
    }

    cursor = keyPos + quotedKey.size();
  }
}

std::optional<std::string> stringAfterKey(const std::string& text, std::string_view key, std::size_t start = 0) {
  const std::optional<std::size_t> colon = colonAfterKey(text, key, start);
  if (!colon.has_value()) {
    return std::nullopt;
  }

  const std::size_t firstQuote = text.find('"', *colon + 1);
  if (firstQuote == std::string::npos) {
    return std::nullopt;
  }

  const std::size_t secondQuote = text.find('"', firstQuote + 1);
  if (secondQuote == std::string::npos) {
    return std::nullopt;
  }

  return text.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

std::optional<std::size_t> matchingClose(const std::string& text, std::size_t open, char openChar, char closeChar) {
  int depth = 0;
  bool inString = false;
  bool escaped = false;

  for (std::size_t i = open; i < text.size(); ++i) {
    const char c = text[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (c == '\\' && inString) {
      escaped = true;
      continue;
    }
    if (c == '"') {
      inString = !inString;
      continue;
    }
    if (inString) {
      continue;
    }
    if (c == openChar) {
      ++depth;
    } else if (c == closeChar) {
      --depth;
      if (depth == 0) {
        return i;
      }
    }
  }

  return std::nullopt;
}

std::optional<std::string> objectAfterKey(const std::string& text, std::string_view key, std::size_t start = 0) {
  const std::optional<std::size_t> colon = colonAfterKey(text, key, start);
  if (!colon.has_value()) {
    return std::nullopt;
  }

  const std::size_t open = text.find('{', *colon + 1);
  if (open == std::string::npos) {
    return std::nullopt;
  }

  const std::optional<std::size_t> close = matchingClose(text, open, '{', '}');
  if (!close.has_value()) {
    return std::nullopt;
  }

  return text.substr(open, *close - open + 1);
}

std::optional<std::string> arrayAfterKey(const std::string& text, std::string_view key, std::size_t start = 0) {
  const std::optional<std::size_t> colon = colonAfterKey(text, key, start);
  if (!colon.has_value()) {
    return std::nullopt;
  }

  const std::size_t open = text.find('[', *colon + 1);
  if (open == std::string::npos) {
    return std::nullopt;
  }

  const std::optional<std::size_t> close = matchingClose(text, open, '[', ']');
  if (!close.has_value()) {
    return std::nullopt;
  }

  return text.substr(open, *close - open + 1);
}

std::vector<std::string> objectsInArray(const std::string& arrayText) {
  std::vector<std::string> objects;
  std::size_t cursor = 0;
  while (true) {
    const std::size_t open = arrayText.find('{', cursor);
    if (open == std::string::npos) {
      break;
    }

    const std::optional<std::size_t> close = matchingClose(arrayText, open, '{', '}');
    if (!close.has_value()) {
      break;
    }

    objects.push_back(arrayText.substr(open, *close - open + 1));
    cursor = *close + 1;
  }
  return objects;
}

std::optional<float> parseFloatAt(const std::string& text, std::size_t& cursor) {
  while (cursor < text.size() && (std::isspace(static_cast<unsigned char>(text[cursor])) || text[cursor] == ',' || text[cursor] == '[')) {
    ++cursor;
  }

  if (cursor >= text.size()) {
    return std::nullopt;
  }

  std::size_t consumed = 0;
  try {
    const float value = std::stof(text.substr(cursor), &consumed);
    cursor += consumed;
    return value;
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<float> numberAfterKey(const std::string& text, std::string_view key) {
  const std::optional<std::size_t> colon = colonAfterKey(text, key);
  if (!colon.has_value()) {
    return std::nullopt;
  }

  std::size_t cursor = *colon + 1;
  return parseFloatAt(text, cursor);
}

std::optional<bool> boolAfterKey(const std::string& text, std::string_view key) {
  const std::optional<std::size_t> colon = colonAfterKey(text, key);
  if (!colon.has_value()) {
    return std::nullopt;
  }

  const std::size_t valueStart = text.find_first_not_of(" \t\r\n", *colon + 1);
  if (valueStart == std::string::npos) {
    return std::nullopt;
  }
  if (text.compare(valueStart, 4, "true") == 0) {
    return true;
  }
  if (text.compare(valueStart, 5, "false") == 0) {
    return false;
  }
  return std::nullopt;
}

template <std::size_t N>
std::optional<std::array<float, N>> floatArrayAfterKey(const std::string& text, std::string_view key) {
  const std::optional<std::string> arrayText = arrayAfterKey(text, key);
  if (!arrayText.has_value()) {
    return std::nullopt;
  }

  std::array<float, N> values{};
  std::size_t cursor = 0;
  for (std::size_t i = 0; i < N; ++i) {
    const std::optional<float> value = parseFloatAt(*arrayText, cursor);
    if (!value.has_value()) {
      return std::nullopt;
    }
    values[i] = *value;
  }
  return values;
}

std::optional<Vec2> vec2AfterKey(const std::string& text, std::string_view key) {
  const std::optional<std::array<float, 2>> values = floatArrayAfterKey<2>(text, key);
  if (!values.has_value()) {
    return std::nullopt;
  }
  return Vec2{.x = (*values)[0], .y = (*values)[1]};
}

std::optional<Color> colorAfterKey(const std::string& text, std::string_view key) {
  const std::optional<std::array<float, 4>> values = floatArrayAfterKey<4>(text, key);
  if (!values.has_value()) {
    return std::nullopt;
  }
  return Color{.r = (*values)[0], .g = (*values)[1], .b = (*values)[2], .a = (*values)[3]};
}

std::optional<ProjectData> parseProject(const std::filesystem::path& projectPath, const std::string& text, std::string& error) {
  ProjectData project;
  project.projectPath = projectPath;
  project.projectDirectory = projectPath.parent_path();

  const std::optional<std::string> name = stringAfterKey(text, "name");
  const std::optional<std::string> mainScene = stringAfterKey(text, "main_scene");
  const std::optional<std::string> scenesArray = arrayAfterKey(text, "scenes");
  if (!name.has_value() || !mainScene.has_value() || !scenesArray.has_value()) {
    error = "Project file is missing name, main_scene, or scenes.";
    return std::nullopt;
  }

  project.name = *name;
  project.mainScene = *mainScene;
  project.scriptEntry = stringAfterKey(text, "entry").value_or(std::string{});

  for (const std::string& sceneObject : objectsInArray(*scenesArray)) {
    const std::optional<std::string> id = stringAfterKey(sceneObject, "id");
    const std::optional<std::string> path = stringAfterKey(sceneObject, "path");
    if (id.has_value() && path.has_value()) {
      project.scenes.push_back(SceneEntry{.id = *id, .path = *path});
    }
  }

  if (project.scenes.empty()) {
    error = "Project does not contain any loadable scenes.";
    return std::nullopt;
  }

  return project;
}

World parseScene(const std::filesystem::path& scenePath, const std::string& text) {
  World world;
  world.scenePath = scenePath;
  world.id = stringAfterKey(text, "id").value_or("scene://unknown");
  world.name = stringAfterKey(text, "name").value_or(world.id);

  const std::optional<std::string> entitiesArray = arrayAfterKey(text, "entities");
  if (!entitiesArray.has_value()) {
    return world;
  }

  for (const std::string& entityObject : objectsInArray(*entitiesArray)) {
    Entity entity;
    entity.id = stringAfterKey(entityObject, "id").value_or("ent_unknown");
    entity.name = stringAfterKey(entityObject, "name").value_or(entity.id);

    if (const std::optional<std::string> transform = objectAfterKey(entityObject, "Transform2D")) {
      Transform2DComponent component;
      if (const std::optional<Vec2> position = vec2AfterKey(*transform, "position")) {
        component.position = *position;
      }
      if (const std::optional<float> rotation = numberAfterKey(*transform, "rotation")) {
        component.rotation = *rotation;
      }
      if (const std::optional<Vec2> scale = vec2AfterKey(*transform, "scale")) {
        component.scale = *scale;
      }
      entity.transform2D = component;
    }

    if (const std::optional<std::string> camera = objectAfterKey(entityObject, "Camera2D")) {
      Camera2DComponent component;
      if (const std::optional<Color> clearColor = colorAfterKey(*camera, "clear_color")) {
        component.clearColor = *clearColor;
      }
      if (const std::optional<float> orthographicSize = numberAfterKey(*camera, "orthographic_size")) {
        component.orthographicSize = *orthographicSize;
      }
      entity.camera2D = component;
    }

    if (const std::optional<std::string> sprite = objectAfterKey(entityObject, "Sprite")) {
      SpriteComponent component;
      component.texture = stringAfterKey(*sprite, "texture").value_or(std::string{});
      component.layer = stringAfterKey(*sprite, "layer").value_or(std::string{});
      entity.sprite = component;
    }

    if (const std::optional<std::string> isoGrid = objectAfterKey(entityObject, "IsoGrid")) {
      IsoGridComponent component;
      if (const std::optional<Vec2> cellSize = vec2AfterKey(*isoGrid, "cell_size")) {
        component.cellSize = *cellSize;
      }
      if (const std::optional<float> width = numberAfterKey(*isoGrid, "width")) {
        component.width = static_cast<int>(*width);
      }
      if (const std::optional<float> height = numberAfterKey(*isoGrid, "height")) {
        component.height = static_cast<int>(*height);
      }
      entity.isoGrid = component;
    }

    if (const std::optional<std::string> isoTransform = objectAfterKey(entityObject, "IsoTransform")) {
      IsoTransformComponent component;
      if (const std::optional<Vec2> tile = vec2AfterKey(*isoTransform, "tile")) {
        component.tile = *tile;
      }
      if (const std::optional<float> height = numberAfterKey(*isoTransform, "height")) {
        component.height = *height;
      }
      if (const std::optional<Vec2> footprint = vec2AfterKey(*isoTransform, "footprint")) {
        component.footprint = *footprint;
      }
      entity.isoTransform = component;
    }

    if (const std::optional<std::string> hitbox = objectAfterKey(entityObject, "HitboxController")) {
      HitboxControllerComponent component;
      if (const std::optional<Vec2> hurtbox = vec2AfterKey(*hitbox, "hurtbox")) {
        component.hurtbox = *hurtbox;
      }
      component.script = stringAfterKey(*hitbox, "script").value_or(std::string{});
      entity.hitboxController = component;
    }

    if (const std::optional<std::string> luaScript = objectAfterKey(entityObject, "LuaScript")) {
      LuaScriptComponent component;
      component.module = stringAfterKey(*luaScript, "module").value_or(std::string{});
      if (const std::optional<std::string> properties = objectAfterKey(*luaScript, "properties")) {
        if (const std::optional<float> speed = numberAfterKey(*properties, "speed")) {
          component.speed = *speed;
        }
        if (const std::optional<float> jumpSpeed = numberAfterKey(*properties, "jump_speed")) {
          component.jumpSpeed = *jumpSpeed;
        }
      }
      entity.luaScript = component;
    }

    if (const std::optional<std::string> buildable = objectAfterKey(entityObject, "Buildable")) {
      BuildableComponent component;
      component.asset = stringAfterKey(*buildable, "asset").value_or(std::string{});
      component.blocksMovement = boolAfterKey(*buildable, "blocks_movement").value_or(false);
      entity.buildable = component;
    }

    if (const std::optional<std::string> rigidbody = objectAfterKey(entityObject, "Rigidbody2D")) {
      Rigidbody2DComponent component;
      component.bodyType = stringAfterKey(*rigidbody, "body_type").value_or("dynamic");
      if (const std::optional<Vec2> velocity = vec2AfterKey(*rigidbody, "velocity")) {
        component.velocity = *velocity;
      }
      if (const std::optional<float> gravityScale = numberAfterKey(*rigidbody, "gravity_scale")) {
        component.gravityScale = *gravityScale;
      }
      if (const std::optional<float> bounciness = numberAfterKey(*rigidbody, "bounciness")) {
        component.bounciness = *bounciness;
      }
      component.lockRotation = boolAfterKey(*rigidbody, "lock_rotation").value_or(true);
      entity.rigidbody2D = component;
    }

    if (const std::optional<std::string> boxCollider = objectAfterKey(entityObject, "BoxCollider2D")) {
      BoxCollider2DComponent component;
      if (const std::optional<Vec2> size = vec2AfterKey(*boxCollider, "size")) {
        component.size = *size;
      }
      if (const std::optional<Vec2> offset = vec2AfterKey(*boxCollider, "offset")) {
        component.offset = *offset;
      }
      component.isTrigger = boolAfterKey(*boxCollider, "is_trigger").value_or(false);
      component.layer = stringAfterKey(*boxCollider, "layer").value_or(std::string{});
      entity.boxCollider2D = component;
    }

    if (const std::optional<std::string> audioSource = objectAfterKey(entityObject, "AudioSource")) {
      AudioSourceComponent component;
      component.clip = stringAfterKey(*audioSource, "clip").value_or(std::string{});
      component.playOnStart = boolAfterKey(*audioSource, "play_on_start").value_or(false);
      component.loop = boolAfterKey(*audioSource, "loop").value_or(false);
      if (const std::optional<float> volume = numberAfterKey(*audioSource, "volume")) {
        component.volume = *volume;
      }
      entity.audioSource = component;
    }

    if (const std::optional<std::string> audioListener = objectAfterKey(entityObject, "AudioListener")) {
      AudioListenerComponent component;
      component.primary = boolAfterKey(*audioListener, "primary").value_or(true);
      entity.audioListener = component;
    }

    if (const std::optional<std::string> videoPlayer = objectAfterKey(entityObject, "VideoPlayer")) {
      VideoPlayerComponent component;
      component.clip = stringAfterKey(*videoPlayer, "clip").value_or(std::string{});
      component.playOnStart = boolAfterKey(*videoPlayer, "play_on_start").value_or(false);
      component.loop = boolAfterKey(*videoPlayer, "loop").value_or(false);
      entity.videoPlayer = component;
    }

    world.entities.push_back(entity);
  }

  return world;
}

void parseHudElement(const std::string& elementObject, World& world) {
  const std::string type = stringAfterKey(elementObject, "type").value_or(std::string{});
  const std::string id = stringAfterKey(elementObject, "id").value_or(std::string{});
  if (id.empty()) {
    return;
  }

  if (type == "rect") {
    HudRectElement rect;
    rect.id = id;
    rect.group = stringAfterKey(elementObject, "group").value_or(std::string{});
    if (const std::optional<Vec2> position = vec2AfterKey(elementObject, "position")) {
      rect.position = *position;
    }
    if (const std::optional<Vec2> size = vec2AfterKey(elementObject, "size")) {
      rect.size = *size;
    }
    if (const std::optional<Color> color = colorAfterKey(elementObject, "color")) {
      rect.color = *color;
    }
    rect.visible = boolAfterKey(elementObject, "visible").value_or(true);
    world.hudRects.push_back(std::move(rect));
    return;
  }

  if (type == "text") {
    HudTextElement text;
    text.id = id;
    text.group = stringAfterKey(elementObject, "group").value_or(std::string{});
    text.text = stringAfterKey(elementObject, "text").value_or(std::string{});
    if (const std::optional<Vec2> position = vec2AfterKey(elementObject, "position")) {
      text.position = *position;
    }
    if (const std::optional<float> scale = numberAfterKey(elementObject, "scale")) {
      text.scale = *scale;
    }
    if (const std::optional<Color> color = colorAfterKey(elementObject, "color")) {
      text.color = *color;
    }
    text.visible = boolAfterKey(elementObject, "visible").value_or(true);
    world.hudText.push_back(std::move(text));
    return;
  }

  if (type == "button") {
    HudButtonElement button;
    button.id = id;
    button.group = stringAfterKey(elementObject, "group").value_or(std::string{});
    button.label = stringAfterKey(elementObject, "label").value_or(id);
    if (const std::optional<Vec2> position = vec2AfterKey(elementObject, "position")) {
      button.position = *position;
    }
    if (const std::optional<Vec2> size = vec2AfterKey(elementObject, "size")) {
      button.size = *size;
    }
    if (const std::optional<float> scale = numberAfterKey(elementObject, "scale")) {
      button.scale = *scale;
    }
    if (const std::optional<Color> color = colorAfterKey(elementObject, "color")) {
      button.color = *color;
    }
    if (const std::optional<Color> hoverColor = colorAfterKey(elementObject, "hover_color")) {
      button.hoverColor = *hoverColor;
    }
    if (const std::optional<Color> textColor = colorAfterKey(elementObject, "text_color")) {
      button.textColor = *textColor;
    }
    button.script = stringAfterKey(elementObject, "script").value_or(std::string{});
    button.visible = boolAfterKey(elementObject, "visible").value_or(true);
    world.hudButtons.push_back(std::move(button));
  }
}

void loadHudFile(World& world, const std::filesystem::path& hudPath, std::string& error) {
  const std::string hudText = readFile(hudPath, error);
  if (!error.empty()) {
    return;
  }

  if (const std::optional<Vec2> canvasSize = vec2AfterKey(hudText, "canvas_size")) {
    if (canvasSize->x > 0.0F && canvasSize->y > 0.0F) {
      world.hudCanvasSize = *canvasSize;
    }
  }

  const std::optional<std::string> elementsArray = arrayAfterKey(hudText, "elements");
  if (!elementsArray.has_value()) {
    return;
  }

  for (const std::string& elementObject : objectsInArray(*elementsArray)) {
    parseHudElement(elementObject, world);
  }
}

} // namespace

std::optional<LoadedProject> loadProject(const std::filesystem::path& projectPath, std::string& error) {
  const ValidationSummary validation = validatePath(projectPath);
  if (hasErrors(validation.diagnostics)) {
    error = "Project validation failed before runtime load.";
    return std::nullopt;
  }

  const std::string projectText = readFile(projectPath, error);
  if (!error.empty()) {
    return std::nullopt;
  }

  std::optional<ProjectData> project = parseProject(projectPath, projectText, error);
  if (!project.has_value()) {
    return std::nullopt;
  }

  const auto mainScene = std::ranges::find_if(project->scenes, [&](const SceneEntry& entry) { return entry.id == project->mainScene; });
  if (mainScene == project->scenes.end()) {
    error = "main_scene does not match any scene entry: " + project->mainScene;
    return std::nullopt;
  }

  std::optional<World> world = loadScene(*project, project->mainScene, error);
  if (!world.has_value()) {
    return std::nullopt;
  }

  return LoadedProject{.project = *project, .world = std::move(*world)};
}

std::optional<World> loadScene(const ProjectData& project, const std::string& sceneId, std::string& error) {
  const auto scene = std::ranges::find_if(project.scenes, [&](const SceneEntry& entry) { return entry.id == sceneId; });
  if (scene == project.scenes.end()) {
    error = "No scene registered with id: " + sceneId;
    return std::nullopt;
  }

  const std::filesystem::path scenePath = project.projectDirectory / scene->path;
  const ValidationSummary sceneValidation = validatePath(scenePath);
  if (hasErrors(sceneValidation.diagnostics)) {
    error = "Scene validation failed before runtime load: " + scenePath.string();
    return std::nullopt;
  }

  const std::string sceneText = readFile(scenePath, error);
  if (!error.empty()) {
    return std::nullopt;
  }

  World world = parseScene(scenePath, sceneText);
  if (const std::optional<std::string> hud = stringAfterKey(sceneText, "hud")) {
    const std::filesystem::path hudPath = scenePath.parent_path() / *hud;
    const ValidationSummary hudValidation = validatePath(hudPath);
    if (hasErrors(hudValidation.diagnostics)) {
      error = "HUD validation failed before runtime load: " + hudPath.string();
      return std::nullopt;
    }
    loadHudFile(world, hudPath, error);
    if (!error.empty()) {
      return std::nullopt;
    }
  }

  return world;
}

} // namespace demi::runtime
