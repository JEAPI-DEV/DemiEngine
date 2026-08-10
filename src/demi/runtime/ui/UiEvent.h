#pragma once

#include "demi/runtime/scene/model/SceneTypes.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace demi::runtime::ui {

enum class UiEventType {
  ValueChanged,
  FocusGained,
  FocusLost,
  Submit,
  Cancel,
  PointerEnter,
  PointerExit,
  Press,
  Release,
  DragStart,
  Drag,
  DragEnd,
  Drop,
  Scroll,
};

struct UiEvent {
  UiEventType type = UiEventType::Submit;
  std::string id;
  std::string relatedId;
  std::string action;
  std::string text;
  std::string source;
  std::int64_t pointerId = -1;
  Vec2 position{};
  Vec2 delta{};
  float value = 0.0F;
  bool checked = false;
  bool cancelled = false;
};

[[nodiscard]] std::string_view uiEventTypeName(UiEventType type);

} // namespace demi::runtime::ui
