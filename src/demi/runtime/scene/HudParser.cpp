#include "demi/runtime/scene/HudParser.h"

#include "demi/runtime/scene/SceneJson.h"

#include <array>
#include <string_view>

namespace demi::runtime::scene_loading {

namespace {

using HudElementParseFn = void (*)(const Json& elementJson, const std::string& id, World& world);

struct HudElementParser {
  std::string_view type;
  HudElementParseFn parse;
};

void parseRect(const Json& json, const std::string& id, World& world) {
  HudRectElement rect;
  rect.id = id;
  rect.group = stringOr(json, "group");
  if (const std::optional<Vec2> position = vec2Field(json, "position")) {
    rect.position = *position;
  }
  if (const std::optional<Vec2> size = vec2Field(json, "size")) {
    rect.size = *size;
  }
  if (const std::optional<Color> color = colorField(json, "color")) {
    rect.color = *color;
  }
  rect.visible = boolField(json, "visible").value_or(true);
  world.hudRects.push_back(std::move(rect));
}

void parseImage(const Json& json, const std::string& id, World& world) {
  HudImageElement image;
  image.id = id;
  image.group = stringOr(json, "group");
  image.texture = stringOr(json, "texture");
  if (const std::optional<Vec2> position = vec2Field(json, "position")) {
    image.position = *position;
  }
  if (const std::optional<Vec2> size = vec2Field(json, "size")) {
    image.size = *size;
  }
  if (const std::optional<Vec2> sourcePosition = vec2Field(json, "source_position")) {
    image.sourcePosition = *sourcePosition;
  }
  if (const std::optional<Vec2> sourceSize = vec2Field(json, "source_size")) {
    image.sourceSize = *sourceSize;
  }
  if (const std::optional<Color> color = colorField(json, "color")) {
    image.color = *color;
  }
  image.visible = boolField(json, "visible").value_or(true);
  world.hudImages.push_back(std::move(image));
}

void parseText(const Json& json, const std::string& id, World& world) {
  HudTextElement text;
  text.id = id;
  text.group = stringOr(json, "group");
  text.text = stringOr(json, "text");
  if (const std::optional<Vec2> position = vec2Field(json, "position")) {
    text.position = *position;
  }
  if (const std::optional<float> scale = numberField(json, "scale")) {
    text.scale = *scale;
  }
  if (const std::optional<float> fontSize = numberField(json, "font_size")) {
    text.fontSize = *fontSize;
  }
  if (const std::optional<Color> color = colorField(json, "color")) {
    text.color = *color;
  }
  text.visible = boolField(json, "visible").value_or(true);
  world.hudText.push_back(std::move(text));
}

void parseButton(const Json& json, const std::string& id, World& world) {
  HudButtonElement button;
  button.id = id;
  button.group = stringOr(json, "group");
  button.label = stringOr(json, "label", id);
  if (const std::optional<Vec2> position = vec2Field(json, "position")) {
    button.position = *position;
  }
  if (const std::optional<Vec2> size = vec2Field(json, "size")) {
    button.size = *size;
  }
  if (const std::optional<float> scale = numberField(json, "scale")) {
    button.scale = *scale;
  }
  if (const std::optional<float> fontSize = numberField(json, "font_size")) {
    button.fontSize = *fontSize;
  }
  if (const std::optional<Color> color = colorField(json, "color")) {
    button.color = *color;
  }
  if (const std::optional<Color> hoverColor = colorField(json, "hover_color")) {
    button.hoverColor = *hoverColor;
  }
  if (const std::optional<Color> textColor = colorField(json, "text_color")) {
    button.textColor = *textColor;
  }
  button.script = stringOr(json, "script");
  button.action = stringOr(json, "action");
  button.visible = boolField(json, "visible").value_or(true);
  world.hudButtons.push_back(std::move(button));
}

constexpr std::array hudElementParsers{
  HudElementParser{.type = "rect", .parse = parseRect},
  HudElementParser{.type = "image", .parse = parseImage},
  HudElementParser{.type = "text", .parse = parseText},
  HudElementParser{.type = "button", .parse = parseButton},
};

void parseHudElement(const Json& elementJson, World& world) {
  const std::string type = stringOr(elementJson, "type");
  const std::string id = stringOr(elementJson, "id");
  if (id.empty()) {
    return;
  }

  for (const HudElementParser& parser : hudElementParsers) {
    if (parser.type == type) {
      parser.parse(elementJson, id, world);
      return;
    }
  }
}

} // namespace

void loadHudFile(World& world, const std::filesystem::path& hudPath, std::string& error) {
  const std::optional<Json> document = readJsonFile(hudPath, error);
  if (!document.has_value()) {
    return;
  }

  if (const std::optional<Vec2> canvasSize = vec2Field(*document, "canvas_size")) {
    if (canvasSize->x > 0.0F && canvasSize->y > 0.0F) {
      world.hudCanvasSize = *canvasSize;
    }
  }

  const Json* elements = arrayField(*document, "elements");
  if (elements == nullptr) {
    return;
  }

  for (const Json& elementJson : *elements) {
    if (elementJson.is_object()) {
      parseHudElement(elementJson, world);
    }
  }
}

} // namespace demi::runtime::scene_loading
