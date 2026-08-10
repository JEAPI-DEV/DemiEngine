#pragma once
#include "demi/runtime/ui/UiModel.h"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
namespace demi::runtime::ui {
class UiInteractionController {
public:
  bool focusNext(UiDocument &document, bool reverse = false) const;
  bool movePointer(UiDocument &document, std::int64_t pointerId, Vec2 position,
                   std::string_view source = "pointer") const;
  bool capturePointer(UiDocument &document, Vec2 position) const;
  bool capturePointer(UiDocument &document, std::int64_t pointerId,
                      Vec2 position, std::string_view source = "pointer") const;
  bool updatePointer(UiDocument &document, Vec2 position) const;
  bool updatePointer(UiDocument &document, std::int64_t pointerId,
                     Vec2 position, std::string_view source = "pointer") const;
  bool scrollPointer(UiDocument &document, std::int64_t pointerId,
                     Vec2 position, Vec2 delta,
                     std::string_view source = "pointer") const;
  void releasePointer(UiDocument &document) const;
  void releasePointer(UiDocument &document, std::int64_t pointerId) const;
  void releasePointer(UiDocument &document, std::int64_t pointerId,
                      Vec2 position, bool cancelled,
                      std::string_view source = "pointer") const;
  void cancel(UiDocument &document, std::string_view source = "keyboard") const;
  [[nodiscard]] bool pointerCaptured(const UiDocument &document,
                                     std::int64_t pointerId) const;
  [[nodiscard]] std::optional<std::string>
  activateFocused(UiDocument &document,
                  std::string_view source = "keyboard") const;
};
} // namespace demi::runtime::ui
