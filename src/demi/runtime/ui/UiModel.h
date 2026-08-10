#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"
#include "demi/runtime/ui/TextEditingEngine.h"
#include "demi/runtime/ui/UiEvent.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
enum class TextWrapMode { None, Word, Grapheme };
enum class TextOverflowMode { Visible, Clip, Ellipsis };

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
  std::string sceneOwner;
  std::string parent;
  std::string type;
  std::string style;
  std::string text;
  std::string placeholder;
  std::string placeholderLocalizationKey;
  std::string localizationKey;
  std::string texture;
  std::string animation;
  std::string action;
  std::string control;
  std::string accessibilityLabel;
  std::string accessibilityDescription;
  std::string script;
  LayoutSpec layout;
  Rect resolved;
  Color color{1.0F, 1.0F, 1.0F, 1.0F};
  Color backgroundColor{0.0F, 0.0F, 0.0F, 0.0F};
  Color borderColor{1.0F, 1.0F, 1.0F, 0.20F};
  Color hoverColor{};
  Color textColor{1.0F, 1.0F, 1.0F, 1.0F};
  Vec2 sourcePosition{};
  Vec2 sourceSize{};
  int layer = 0;
  int animationFrame = 0;
  float value = 0.0F;
  float minimum = 0.0F;
  float maximum = 1.0F;
  float fontSize = 20.0F;
  float lineSpacing = 0.0F;
  std::size_t maxLines = 0;
  TextWrapMode textWrap = TextWrapMode::None;
  TextOverflowMode textOverflow = TextOverflowMode::Clip;
  Alignment textHorizontalAlignment = Alignment::Start;
  Alignment textVerticalAlignment = Alignment::Start;
  float scale = 1.0F;
  float cornerRadius = 0.0F;
  float borderWidth = 0.0F;
  float radius = 0.0F;
  float deadzone = 0.15F;
  bool visible = true;
  bool disabled = false;
  bool focusable = false;
  bool checked = false;
  bool hovered = false;
  bool accessibilityHidden = false;
  TextEditState textEdit;
};

struct UiNodeHandle {
  std::string id;
  std::uint64_t generation = 0;
  [[nodiscard]] explicit operator bool() const {
    return !id.empty() && generation != 0;
  }
  auto operator<=>(const UiNodeHandle &) const = default;
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
  Insets safeArea{};
  std::vector<UiNode> nodes;
  std::unordered_map<std::string, UiStyle> styles;
  std::unordered_map<std::string, std::string> localization;
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
      locales;
  std::string locale;
  std::unordered_map<std::string, std::string> actionMap;
  std::unordered_map<std::string, UiActionEffect> actionEffects;
  std::string focusedId;
  std::unordered_map<std::int64_t, std::string> pointerCaptures;
  std::unordered_map<std::int64_t, std::string> pointerHoverIds;
  std::unordered_map<std::int64_t, Vec2> pointerPositions;
  std::unordered_map<std::int64_t, Vec2> pointerPressPositions;
  std::unordered_set<std::int64_t> draggingPointers;
  std::vector<UiEvent> events;
  std::unordered_map<std::string, std::uint64_t> generations;
  std::uint64_t nextGeneration = 1;
};

} // namespace demi::runtime::ui
