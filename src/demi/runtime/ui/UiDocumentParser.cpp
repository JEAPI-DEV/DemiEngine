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
  node.text = scene_loading::stringOr(json, "text",
                                      scene_loading::stringOr(json, "label"));
  node.placeholder = scene_loading::stringOr(json, "placeholder");
  node.localizationKey = scene_loading::stringOr(json, "localization_key");
  node.texture = scene_loading::stringOr(json, "texture");
  node.action = scene_loading::stringOr(json, "action");
  node.accessibilityLabel =
      scene_loading::stringOr(json, "accessibility_label");
  if (auto value = scene_loading::vec2Field(json, "position"))
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
  node.value = scene_loading::numberField(json, "value").value_or(0.0F);
  node.minimum = scene_loading::numberField(json, "minimum").value_or(0.0F);
  node.maximum = scene_loading::numberField(json, "maximum").value_or(1.0F);
  node.fontSize = scene_loading::numberField(json, "font_size").value_or(20.0F);
  node.visible = scene_loading::boolField(json, "visible").value_or(true);
  node.disabled = scene_loading::boolField(json, "disabled").value_or(false);
  node.focusable =
      scene_loading::boolField(json, "focusable")
          .value_or(node.type == "button" || node.type == "toggle" ||
                    node.type == "slider" || node.type == "text_input");
  node.checked = scene_loading::boolField(json, "checked").value_or(false);
  if (!node.localizationKey.empty()) {
    const auto localized = out.localization.find(node.localizationKey);
    if (localized != out.localization.end())
      node.text = localized->second;
  }
  const std::string placeholderLocalizationKey =
      scene_loading::stringOr(json, "placeholder_localization_key");
  if (!placeholderLocalizationKey.empty()) {
    const auto localized = out.localization.find(placeholderLocalizationKey);
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
      result.styles[name] = style;
    }
  }
  if (const Json *root = scene_loading::objectField(document, "root"))
    parseNode(*root, {}, result);
  else if (const Json *elements =
               scene_loading::arrayField(document, "elements"))
    for (const Json &element : *elements)
      parseNode(element, {}, result);
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
  return result;
}
} // namespace demi::runtime::ui
