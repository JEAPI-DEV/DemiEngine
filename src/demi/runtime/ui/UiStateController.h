#pragma once

#include "demi/runtime/ui/UiModel.h"

#include <string_view>

namespace demi::runtime::ui {

class UiStateController {
public:
  [[nodiscard]] UiNode *find(UiDocument &document, std::string_view id) const;
  [[nodiscard]] const UiNode *find(const UiDocument &document,
                                   std::string_view id) const;
  bool setRect(UiDocument &document, std::string_view id, Vec2 position,
               Vec2 size) const;
  bool setPosition(UiDocument &document, std::string_view id,
                   Vec2 position) const;
  bool setSize(UiDocument &document, std::string_view id, Vec2 size) const;
  bool setText(UiDocument &document, std::string_view id,
               std::string text) const;
  bool setValue(UiDocument &document, std::string_view id, float value) const;
  bool setChecked(UiDocument &document, std::string_view id,
                  bool checked) const;
  bool setDisabled(UiDocument &document, std::string_view id,
                   bool disabled) const;
  bool setVisible(UiDocument &document, std::string_view id,
                  bool visible) const;
};

} // namespace demi::runtime::ui
