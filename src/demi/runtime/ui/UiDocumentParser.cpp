#include "demi/runtime/ui/UiDocumentParser.h"
#include "demi/runtime/scene/SceneJson.h"
#include <nlohmann/json.hpp>
namespace demi::runtime::ui {
namespace {
using Json = nlohmann::json;
Insets insets(const Json &value) {
  if (value.is_number()) {
    const float v = value.get<float>();
    return {v, v, v, v};
  }
  if (value.is_array() && value.size() == 4)
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
void parseNode(const Json &json, const std::string &parent, UiDocument &out) {
  if (!json.is_object())
    return;
  UiNode node;
  node.id = scene_loading::stringOr(json, "id");
  if (node.id.empty())
    return;
  node.parent = scene_loading::stringOr(json, "parent", parent);
  node.type = scene_loading::stringOr(json, "type", "container");
  node.style = scene_loading::stringOr(json, "style");
  node.text = scene_loading::stringOr(json, "text");
  node.placeholder = scene_loading::stringOr(json, "placeholder");
  node.localizationKey = scene_loading::stringOr(json, "localization_key");
  node.texture = scene_loading::stringOr(json, "texture");
  node.action = scene_loading::stringOr(json, "action");
  node.control = scene_loading::stringOr(json, "control");
  node.script = scene_loading::stringOr(json, "script");
  node.accessibilityLabel =
      scene_loading::stringOr(json, "accessibility_label");
  node.accessibilityDescription =
      scene_loading::stringOr(json, "accessibility_description");
  node.accessibilityHidden =
      scene_loading::boolField(json, "accessibility_hidden").value_or(false);
  node.group = scene_loading::stringOr(json, "group");
  if (auto value = scene_loading::vec2Field(json, "position"))
    node.layout.position = *value;
  else if (auto value = scene_loading::vec2Field(json, "center"))
    node.layout.position = *value;
  if (auto value = scene_loading::vec2Field(json, "size"))
    node.layout.size = *value;
  if (auto value = scene_loading::vec2Field(json, "anchor_min"))
    node.layout.anchorMin = *value;
  if (auto value = scene_loading::vec2Field(json, "anchor_max"))
    node.layout.anchorMax = *value;
  if (auto value = scene_loading::vec2Field(json, "min_size"))
    node.layout.minSize = *value;
  if (auto value = scene_loading::vec2Field(json, "max_size"))
    node.layout.maxSize = *value;
  if (json.contains("margin"))
    node.layout.margin = insets(json["margin"]);
  if (json.contains("padding"))
    node.layout.padding = insets(json["padding"]);
  node.layout.direction = direction(scene_loading::stringOr(json, "layout"));
  node.layout.alignment = alignment(scene_loading::stringOr(json, "alignment"));
  node.layout.gap = scene_loading::numberField(json, "gap").value_or(0.0F);
  node.layout.columns = static_cast<int>(
      scene_loading::numberField(json, "columns").value_or(1.0F));
  if (auto value = scene_loading::colorField(json, "color"))
    node.color = *value;
  if (auto value = scene_loading::colorField(json, "background_color"))
    node.backgroundColor = *value;
  if (auto value = scene_loading::colorField(json, "border_color"))
    node.borderColor = *value;
  if (auto value = scene_loading::colorField(json, "hover_color"))
    node.hoverColor = *value;
  if (auto value = scene_loading::colorField(json, "text_color"))
    node.textColor = *value;
  node.value = scene_loading::numberField(json, "value").value_or(0.0F);
  node.minimum = scene_loading::numberField(json, "minimum").value_or(0.0F);
  node.maximum = scene_loading::numberField(json, "maximum").value_or(1.0F);
  if (auto value = scene_loading::numberField(json, "font_size"))
    node.fontSize = *value;
  node.lineSpacing =
      scene_loading::numberField(json, "line_spacing").value_or(0.0F);
  node.maxLines = static_cast<std::size_t>(std::max(
      scene_loading::numberField(json, "max_lines").value_or(0.0F), 0.0F));
  node.textWrap = textWrap(scene_loading::stringOr(json, "text_wrap"));
  node.textOverflow =
      textOverflow(scene_loading::stringOr(json, "text_overflow"));
  node.textHorizontalAlignment =
      alignment(scene_loading::stringOr(json, "text_alignment"));
  node.textVerticalAlignment =
      alignment(scene_loading::stringOr(json, "text_vertical_alignment"));
  node.cornerRadius =
      scene_loading::numberField(json, "corner_radius").value_or(0.0F);
  node.borderWidth =
      scene_loading::numberField(json, "border_width").value_or(0.0F);
  node.radius = scene_loading::numberField(json, "radius").value_or(0.0F);
  node.deadzone = scene_loading::numberField(json, "deadzone").value_or(0.15F);
  node.layer = static_cast<int>(
      scene_loading::numberField(json, "layer").value_or(0.0F));
  if (auto value = scene_loading::vec2Field(json, "source_position"))
    node.sourcePosition = *value;
  if (auto value = scene_loading::vec2Field(json, "source_size"))
    node.sourceSize = *value;
  if (auto value = scene_loading::stringOr(json, "animation"); !value.empty()) {
    node.animation = value;
    node.animationFrame = static_cast<int>(
        scene_loading::numberField(json, "animation_frame").value_or(0.0F));
  }
  node.visible = scene_loading::boolField(json, "visible").value_or(true);
  node.disabled = scene_loading::boolField(json, "disabled").value_or(false);
  node.focusable =
      scene_loading::boolField(json, "focusable")
          .value_or(node.type == "button" || node.type == "toggle" ||
                    node.type == "slider" || node.type == "text_input" ||
                    node.type == "virtual_button" ||
                    node.type == "virtual_stick");
  node.checked = scene_loading::boolField(json, "checked").value_or(false);
  if (!node.localizationKey.empty()) {
    const auto localized = out.localization.find(node.localizationKey);
    if (localized != out.localization.end())
      node.text = localized->second;
  }
  node.placeholderLocalizationKey =
      scene_loading::stringOr(json, "placeholder_localization_key");
  if (!node.placeholderLocalizationKey.empty()) {
    const auto localized =
        out.localization.find(node.placeholderLocalizationKey);
    if (localized != out.localization.end())
      node.placeholder = localized->second;
  }
  const std::string id = node.id;
  out.nodes.push_back(std::move(node));
  if (const Json *children = scene_loading::arrayField(json, "children"))
    for (const Json &child : *children)
      parseNode(child, id, out);
}
} // namespace
UiDocument parseUiDocument(const nlohmann::json &document) {
  UiDocument result;
  if (auto value = scene_loading::vec2Field(document, "canvas_size"))
    result.canvasSize = *value;
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
      if (auto color = scene_loading::colorField(value, "color"))
        style.color = *color;
      if (auto color = scene_loading::colorField(value, "background_color"))
        style.backgroundColor = *color;
      if (value.contains("padding"))
        style.padding = insets(value["padding"]);
      style.gap = scene_loading::numberField(value, "gap").value_or(0.0F);
      result.styles[name] = std::move(style);
    }
  }
  if (const Json *root = scene_loading::objectField(document, "root"))
    parseNode(*root, {}, result);
  for (UiNode &node : result.nodes)
    if (const auto style = result.styles.find(node.style);
        style != result.styles.end()) {
      node.color = style->second.color;
      node.backgroundColor = style->second.backgroundColor;
      if (node.layout.padding.left == 0 && node.layout.padding.top == 0 &&
          node.layout.padding.right == 0 && node.layout.padding.bottom == 0)
        node.layout.padding = style->second.padding;
      if (node.layout.gap == 0)
        node.layout.gap = style->second.gap;
    }
  for (const UiNode &node : result.nodes)
    result.generations[node.id] = result.nextGeneration++;
  return result;
}
} // namespace demi::runtime::ui
