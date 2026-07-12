#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime::ui {

struct Insets {
  float left = 0.0F;
  float top = 0.0F;
  float right = 0.0F;
  float bottom = 0.0F;
};

struct Rect {
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;
};

enum class LayoutDirection { None, Row, Column, Grid };
enum class Alignment { Start, Center, End, Stretch };

struct LayoutSpec {
  Vec2 position{};
  Vec2 size{};
  Vec2 anchorMin{};
  Vec2 anchorMax{};
  Vec2 minSize{};
  Vec2 maxSize{};
  Insets margin{};
  Insets padding{};
  LayoutDirection direction = LayoutDirection::None;
  Alignment alignment = Alignment::Start;
  float gap = 0.0F;
  int columns = 1;
};

struct UiNode {
  std::string id;
  std::string parent;
  std::string type;
  std::string style;
  std::string text;
  std::string placeholder;
  std::string localizationKey;
  std::string texture;
  std::string action;
  std::string accessibilityLabel;
  LayoutSpec layout;
  Rect resolved;
  Color color{1.0F, 1.0F, 1.0F, 1.0F};
  Color backgroundColor{};
  float value = 0.0F;
  float minimum = 0.0F;
  float maximum = 1.0F;
  float fontSize = 20.0F;
  bool visible = true;
  bool disabled = false;
  bool focusable = false;
  bool checked = false;
};

struct UiStyle {
  Color color{1.0F, 1.0F, 1.0F, 1.0F};
  Color backgroundColor{};
  Insets padding{};
  float gap = 0.0F;
};

struct UiActionEffect {
  std::vector<std::string> show;
  std::vector<std::string> hide;
  std::string focus;
};

struct UiDocument {
  Vec2 canvasSize{960.0F, 540.0F};
  std::vector<UiNode> nodes;
  std::unordered_map<std::string, UiStyle> styles;
  std::unordered_map<std::string, std::string> localization;
  std::unordered_map<std::string, std::string> actionMap;
  std::unordered_map<std::string, UiActionEffect> actionEffects;
  std::string focusedId;
  std::string pointerCaptureId;
};

} // namespace demi::runtime::ui
