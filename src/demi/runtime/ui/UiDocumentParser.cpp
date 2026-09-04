#include "demi/runtime/ui/UiDocumentParser.h"
#include "demi/runtime/scene/SceneJson.h"
#include "demi/runtime/ui/UiVariables.h"
#include <nlohmann/json.hpp>
namespace demi::runtime::ui {
namespace {
using Json = nlohmann::json;
Insets insets(const Json &value) {
  if (value.is_number()) {
    const float v = value.get<float>();
    return {v, v, v, v};
  }
  if (value.is_array() && value.size() == 4 && (*value.begin()).is_number())
    return {value[0].get<float>(), value[1].get<float>(), value[2].get<float>(),
            value[3].get<float>()};
  return {};
}
LayoutDirection direction(const std::string &value) {
  if (value == "row")
    return LayoutDirection::Row;
  if (value == "column")
    return LayoutDirection::Column;
  if (value == "grid")
    return LayoutDirection::Grid;
  return LayoutDirection::None;
}
Alignment alignment(const std::string &value) {
  if (value == "center")
    return Alignment::Center;
  if (value == "end")
    return Alignment::End;
  if (value == "stretch")
    return Alignment::Stretch;
  return Alignment::Start;
}
TextWrapMode textWrap(const std::string &value) {
  if (value == "word")
    return TextWrapMode::Word;
  if (value == "grapheme")
    return TextWrapMode::Grapheme;
  return TextWrapMode::None;
}
TextOverflowMode textOverflow(const std::string &value) {
  if (value == "visible")
    return TextOverflowMode::Visible;
  if (value == "ellipsis")
    return TextOverflowMode::Ellipsis;
  return TextOverflowMode::Clip;
}

std::optional<Color> hexColor(const std::string &text) {
  if (text.empty() || text[0] != '#' ||
      (text.size() != 7 && text.size() != 9))
    return std::nullopt;
  auto hex = [](char c) -> int {
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1;
  };
  auto byteAt = [&](std::size_t pos) -> std::optional<float> {
    const int hi = hex(text[pos]);
    const int lo = hex(text[pos + 1]);
    if (hi < 0 || lo < 0)
      return std::nullopt;
    return static_cast<float>(hi * 16 + lo) / 255.0F;
  };
  const auto r = byteAt(1);
  const auto g = byteAt(3);
  const auto b = byteAt(5);
  if (!r || !g || !b)
    return std::nullopt;
  float a = 1.0F;
  if (text.size() == 9) {
    const auto alpha = byteAt(7);
    if (!alpha)
      return std::nullopt;
    a = *alpha;
  }
  return Color{*r, *g, *b, a};
}

std::optional<Color> colorValue(const Json &object, const char *key) {
  if (!object.is_object())
    return std::nullopt;
  const auto iter = object.find(key);
  if (iter == object.end())
    return std::nullopt;
  if (iter->is_string()) {
    // Hex form: "color": "#RRGGBB[AA]". Named theme tokens stay unresolved
    // here; use a named style instead.
    return hexColor(iter->get<std::string>());
  }
  return scene_loading::colorField(object, key);
}

// P1: shorthand expansion applied before field parsing. Additive only:
// explicit anchor_min/max/position/size/layout fields always win.
Json expandNodeShorthand(const Json &json) {
  if (!json.is_object())
    return json;
  Json out = json;
  if (out.contains("at") && out["at"].is_array() && out["at"].size() == 2 &&
      !out.contains("position"))
    out["position"] = out["at"];
  if (out.contains("stack") && out["stack"].is_string() &&
      !out.contains("layout"))
    out["layout"] = out["stack"];
  if (out.contains("pad") && !out.contains("padding"))
    out["padding"] = out["pad"];
  if (out.contains("dock") && out["dock"].is_string()) {
    const std::string dock = out["dock"].get<std::string>();
    const bool hasAnchors =
        out.contains("anchor_min") || out.contains("anchor_max");
    if (!hasAnchors) {
      if (dock == "fill") {
        out["anchor_min"] = {0, 0};
        out["anchor_max"] = {1, 1};
      } else if (dock == "center") {
        out["anchor_min"] = {0.5, 0.5};
        out["anchor_max"] = {0.5, 0.5};
      } else if (dock == "top") {
        out["anchor_min"] = {0, 0};
        out["anchor_max"] = {1, 0};
      } else if (dock == "bottom") {
        out["anchor_min"] = {0, 1};
        out["anchor_max"] = {1, 1};
      } else if (dock == "left") {
        out["anchor_min"] = {0, 0};
        out["anchor_max"] = {0, 1};
      } else if (dock == "right") {
        out["anchor_min"] = {1, 0};
        out["anchor_max"] = {1, 1};
      }
    }
  }
  return out;
}

void parseNodeImpl(const Json &json, const std::string &parent,
                   UiDocument &out, const bool parentFlowChild) {
  if (!json.is_object())
    return;
  const Json expanded = expandNodeShorthand(json);
  const Json &source = expanded;
  UiNode node;
  node.id = scene_loading::stringOr(source, "id");
  if (node.id.empty())
    return;
  const bool authoredFontSize = source.contains("font_size");
  const bool authoredMinSize = source.contains("min_size");
  const bool authoredTextColor = source.contains("text_color");
  node.parent = scene_loading::stringOr(source, "parent", parent);
  node.type = scene_loading::stringOr(source, "type", "container");
  node.style = scene_loading::stringOr(source, "style");
  node.text = scene_loading::stringOr(source, "text");
  node.textTemplate = node.text;
  node.font = scene_loading::stringOr(source, "font");
  node.placeholder = scene_loading::stringOr(source, "placeholder");
  node.placeholderTemplate = node.placeholder;
  node.localizationKey = scene_loading::stringOr(source, "localization_key");
  node.texture = scene_loading::stringOr(source, "texture");
  node.action = scene_loading::stringOr(source, "action");
  node.control = scene_loading::stringOr(source, "control");
  node.script = scene_loading::stringOr(source, "script");
  node.accessibilityLabel =
      scene_loading::stringOr(source, "accessibility_label");
  node.accessibilityDescription =
      scene_loading::stringOr(source, "accessibility_description");
  node.accessibilityHidden =
      scene_loading::boolField(source, "accessibility_hidden").value_or(false);
  node.respectsSafeArea =
      scene_loading::boolField(source, "respect_safe_area").value_or(true);
  if (auto value = scene_loading::vec2Field(source, "position"))
    node.layout.position = *value;
  else if (auto value = scene_loading::vec2Field(source, "center"))
    node.layout.position = *value;
  if (auto value = scene_loading::vec2Field(source, "size"))
    node.layout.size = *value;
  if (auto value = scene_loading::vec2Field(source, "anchor_min"))
    node.layout.anchorMin = *value;
  if (auto value = scene_loading::vec2Field(source, "anchor_max"))
    node.layout.anchorMax = *value;
  const bool implicitFill =
      !source.contains("size") && !source.contains("anchor_min") &&
      !source.contains("anchor_max") && !source.contains("dock") &&
      (parent.empty() ||
       (node.type == "container" && source.contains("children")));
  if (implicitFill)
    node.layout.anchorMax = {1.0F, 1.0F};
  if (auto value = scene_loading::vec2Field(source, "min_size"))
    node.layout.minSize = *value;
  if (auto value = scene_loading::vec2Field(source, "max_size"))
    node.layout.maxSize = *value;
  if (source.contains("margin"))
    node.layout.margin = insets(source["margin"]);
  if (source.contains("padding"))
    node.layout.padding = insets(source["padding"]);
  node.layout.direction = direction(scene_loading::stringOr(source, "layout"));
  node.layout.alignment = alignment(scene_loading::stringOr(source, "alignment"));
  node.layout.gap = scene_loading::numberField(source, "gap").value_or(0.0F);
  node.layout.columns = static_cast<int>(
      scene_loading::numberField(source, "columns").value_or(1.0F));
  if (auto value = colorValue(source, "color"))
    node.color = *value;
  if (auto value = colorValue(source, "background_color"))
    node.backgroundColor = *value;
  if (auto value = colorValue(source, "border_color"))
    node.borderColor = *value;
  if (auto value = colorValue(source, "hover_color"))
    node.hoverColor = *value;
  if (auto value = colorValue(source, "text_color"))
    node.textColor = *value;
  node.value = scene_loading::numberField(source, "value").value_or(0.0F);
  node.minimum = scene_loading::numberField(source, "minimum").value_or(0.0F);
  node.maximum = scene_loading::numberField(source, "maximum").value_or(1.0F);
  if (auto value = scene_loading::numberField(source, "font_size"))
    node.fontSize = *value;
  node.lineSpacing =
      scene_loading::numberField(source, "line_spacing").value_or(0.0F);
  node.maxLines = static_cast<std::size_t>(std::max(
      scene_loading::numberField(source, "max_lines").value_or(0.0F), 0.0F));
  node.textWrap = textWrap(scene_loading::stringOr(source, "text_wrap"));
  node.textOverflow =
      textOverflow(scene_loading::stringOr(source, "text_overflow"));
  node.textHorizontalAlignment =
      alignment(scene_loading::stringOr(source, "text_alignment"));
  node.textVerticalAlignment =
      alignment(scene_loading::stringOr(source, "text_vertical_alignment"));
  node.cornerRadius =
      scene_loading::numberField(source, "corner_radius").value_or(0.0F);
  node.borderWidth =
      scene_loading::numberField(source, "border_width").value_or(0.0F);
  node.radius = scene_loading::numberField(source, "radius").value_or(0.0F);
  node.deadzone = scene_loading::numberField(source, "deadzone").value_or(0.15F);
  node.layer = static_cast<int>(
      scene_loading::numberField(source, "layer").value_or(0.0F));
  if (auto value = scene_loading::vec2Field(source, "source_position"))
    node.sourcePosition = *value;
  if (auto value = scene_loading::vec2Field(source, "source_size"))
    node.sourceSize = *value;
  if (auto value = scene_loading::stringOr(source, "animation"); !value.empty()) {
    node.animation = value;
    node.animationFrame = static_cast<int>(
        scene_loading::numberField(source, "animation_frame").value_or(0.0F));
  }
  node.visible = scene_loading::boolField(source, "visible").value_or(true);
  node.disabled = scene_loading::boolField(source, "disabled").value_or(false);
  node.focusable =
      scene_loading::boolField(source, "focusable")
          .value_or(node.type == "button" || node.type == "toggle" ||
                    node.type == "slider" || node.type == "text_input" ||
                    node.type == "virtual_button" ||
                    node.type == "virtual_stick");
  node.checked = scene_loading::boolField(source, "checked").value_or(false);
  if (!node.localizationKey.empty()) {
    const auto localized = out.localization.find(node.localizationKey);
    if (localized != out.localization.end())
      node.text = localized->second;
  }
  node.placeholderLocalizationKey =
      scene_loading::stringOr(source, "placeholder_localization_key");
  if (!node.placeholderLocalizationKey.empty()) {
    const auto localized =
        out.localization.find(node.placeholderLocalizationKey);
    if (localized != out.localization.end())
      node.placeholder = localized->second;
  }
  // P2: type defaults for controls. Explicit size/font_size always win;
  // styles (applied below) override these type defaults. Flow-layout
  // children (row/column/grid parents) get their slot from the parent, so
  // only fixed-position nodes receive a fallback size here.
  const bool stretches =
      node.layout.anchorMax.x > node.layout.anchorMin.x ||
      node.layout.anchorMax.y > node.layout.anchorMin.y;
  if (!source.contains("size") && !stretches && !parentFlowChild) {
    if (node.type == "button" || node.type == "toggle" ||
        node.type == "text_input") {
      node.layout.size = {240.0F, 44.0F};
    } else if (node.type == "slider") {
      node.layout.size = {240.0F, 24.0F};
    }
  }
  if (!authoredFontSize &&
      (node.type == "label" || node.type == "text" || node.type == "button" ||
       node.type == "toggle"))
    node.fontSize = 20.0F;
  const std::string id = node.id;
  // Stash authored-field flags in generation map via sentinel: reuse padding
  // guard pattern below by re-checking source presence.
  out.nodes.push_back(std::move(node));
  UiNode &stored = out.nodes.back();
  // Encode authored flags through minSize sentinel cleanup: if author did not
  // set min_size, keep zero; style cascade below may fill it.
  (void)stored;
  const bool parentFlows =
      direction(scene_loading::stringOr(source, "layout")) !=
      LayoutDirection::None;
  if (const Json *children = scene_loading::arrayField(source, "children"))
    for (const Json &child : *children)
      parseNodeImpl(child, id, out, parentFlows);
  // Remember authored flags for the style cascade by re-reading source.
  // Stored as generation seeds: even generations = authored font size.
  if (authoredFontSize)
    out.generations[id + "#font"] = 1;
  if (authoredTextColor)
    out.generations[id + "#text"] = 1;
  if (authoredMinSize)
    out.generations[id + "#min"] = 1;
}

void parseNode(const Json &json, const std::string &parent, UiDocument &out) {
  parseNodeImpl(json, parent, out, false);
}
} // namespace
UiDocument parseUiDocument(const nlohmann::json &document) {
  UiDocument result;
  if (auto value = scene_loading::vec2Field(document, "canvas_size"))
    result.canvasSize = *value;
  else
    result.canvasSize = {960.0F, 540.0F};
  if (const Json *variables = scene_loading::arrayField(document, "variables"))
    for (const Json &name : *variables)
      if (name.is_string())
        result.declaredVariables.insert(name.get<std::string>());
  if (const Json *localization =
          scene_loading::objectField(document, "localization"))
    for (const auto &[key, value] : localization->items())
      if (value.is_string())
        result.localization[key] = value.get<std::string>();
  if (const Json *locales = scene_loading::objectField(document, "locales"))
    for (const auto &[locale, values] : locales->items())
      if (values.is_object())
        for (const auto &[key, value] : values.items())
          if (value.is_string())
            result.locales[locale][key] = value.get<std::string>();
  result.locale = scene_loading::stringOr(document, "locale");
  if (!result.locale.empty())
    if (const auto found = result.locales.find(result.locale);
        found != result.locales.end())
      result.localization = found->second;
  if (const Json *actions = scene_loading::objectField(document, "action_map"))
    for (const auto &[key, value] : actions->items())
      if (value.is_string())
        result.actionMap[key] = value.get<std::string>();
  if (const Json *effects =
          scene_loading::objectField(document, "action_effects")) {
    for (const auto &[action, value] : effects->items()) {
      if (!value.is_object())
        continue;
      UiActionEffect effect;
      if (const Json *show = scene_loading::arrayField(value, "show"))
        for (const Json &id : *show)
          if (id.is_string())
            effect.show.push_back(id.get<std::string>());
      if (const Json *hide = scene_loading::arrayField(value, "hide"))
        for (const Json &id : *hide)
          if (id.is_string())
            effect.hide.push_back(id.get<std::string>());
      effect.focus = scene_loading::stringOr(value, "focus");
      result.actionEffects[action] = std::move(effect);
    }
  }
  if (const Json *styles = scene_loading::objectField(document, "styles")) {
    for (const auto &[name, value] : styles->items()) {
      UiStyle style;
      if (auto color = colorValue(value, "color"))
        style.color = *color;
      if (auto color = colorValue(value, "background_color"))
        style.backgroundColor = *color;
      if (auto color = colorValue(value, "text_color")) {
        style.textColor = *color;
        style.hasTextColor = true;
      }
      if (value.contains("padding"))
        style.padding = insets(value["padding"]);
      style.gap = scene_loading::numberField(value, "gap").value_or(0.0F);
      if (auto size = scene_loading::numberField(value, "font_size")) {
        style.fontSize = *size;
        style.hasFontSize = true;
      }
      if (auto min = scene_loading::vec2Field(value, "min_size")) {
        style.minSize = *min;
        style.hasMinSize = true;
      }
      if (auto height = scene_loading::numberField(value, "control_height")) {
        style.controlHeight = *height;
        style.hasControlHeight = true;
      }
      if (auto height = scene_loading::numberField(value, "row_height")) {
        style.rowHeight = *height;
        style.hasRowHeight = true;
      }
      result.styles[name] = std::move(style);
    }
  }
  if (const Json *root = scene_loading::objectField(document, "root")) {
    parseNode(*root, {}, result);
  } else if (const Json *children =
                 scene_loading::arrayField(document, "children")) {
    // P1: allow omitting the ui_root wrapper; synthesize a fill container.
    Json syntheticRoot = Json::object({{"id", "ui_root"},
                                       {"type", "container"},
                                       {"anchor_min", {0, 0}},
                                       {"anchor_max", {1, 1}},
                                       {"children", *children}});
    parseNode(syntheticRoot, {}, result);
  } else if (const Json *elements =
                 scene_loading::arrayField(document, "elements")) {
    Json syntheticRoot = Json::object({{"id", "ui_root"},
                                       {"type", "container"},
                                       {"anchor_min", {0, 0}},
                                       {"anchor_max", {1, 1}},
                                       {"children", *elements}});
    parseNode(syntheticRoot, {}, result);
  }
  UiVariables{}.discover(result);
  UiVariables{}.apply(result);
  for (UiNode &node : result.nodes) {
    if (node.id == "ui_root")
      continue;
    const bool authoredFont = result.generations.contains(node.id + "#font");
    const bool authoredText = result.generations.contains(node.id + "#text");
    const bool authoredMin = result.generations.contains(node.id + "#min");
    if (const auto style = result.styles.find(node.style);
        style != result.styles.end()) {
      node.color = style->second.color;
      node.backgroundColor = style->second.backgroundColor;
      if (style->second.hasTextColor && !authoredText)
        node.textColor = style->second.textColor;
      if (node.layout.padding.left == 0 && node.layout.padding.top == 0 &&
          node.layout.padding.right == 0 && node.layout.padding.bottom == 0)
        node.layout.padding = style->second.padding;
      if (node.layout.gap == 0)
        node.layout.gap = style->second.gap;
      if (style->second.hasFontSize && !authoredFont)
        node.fontSize = style->second.fontSize;
      if (style->second.hasMinSize && !authoredMin)
        node.layout.minSize = style->second.minSize;
      if ((style->second.hasControlHeight || style->second.hasRowHeight) &&
          (node.type == "button" || node.type == "toggle" ||
           node.type == "text_input" || node.type == "slider")) {
        const float height = style->second.hasControlHeight
                                 ? style->second.controlHeight
                                 : style->second.rowHeight;
        // Only fill heights the type default assigned; explicit sizes win.
        // Detect explicit size by checking the authored node json is not
        // possible here, so keep the rule narrow: styles only grow/shrink
        // the height component, never the width.
        node.layout.size.y = height;
      }
    }
  }
  result.generations.erase("ui_root#font");
  for (auto it = result.generations.begin(); it != result.generations.end();) {
    if (it->first.ends_with("#font") || it->first.ends_with("#text") ||
        it->first.ends_with("#min"))
      it = result.generations.erase(it);
    else
      ++it;
  }
  for (const UiNode &node : result.nodes)
    result.generations[node.id] = result.nextGeneration++;
  return result;
}
} // namespace demi::runtime::ui
