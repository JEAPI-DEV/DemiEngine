#include "demi/runtime/ui/UiEvent.h"

namespace demi::runtime::ui {

std::string_view uiEventTypeName(const UiEventType type) {
  switch (type) {
  case UiEventType::ValueChanged:
    return "value_changed";
  case UiEventType::FocusGained:
    return "focus_gained";
  case UiEventType::FocusLost:
    return "focus_lost";
  case UiEventType::Submit:
    return "submit";
  case UiEventType::Cancel:
    return "cancel";
  case UiEventType::PointerEnter:
    return "pointer_enter";
  case UiEventType::PointerExit:
    return "pointer_exit";
  case UiEventType::Press:
    return "press";
  case UiEventType::Release:
    return "release";
  case UiEventType::DragStart:
    return "drag_start";
  case UiEventType::Drag:
    return "drag";
  case UiEventType::DragEnd:
    return "drag_end";
  case UiEventType::Drop:
    return "drop";
  case UiEventType::Scroll:
    return "scroll";
  }
  return "unknown";
}

} // namespace demi::runtime::ui
