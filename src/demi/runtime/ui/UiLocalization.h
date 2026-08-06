#pragma once
#include "demi/runtime/ui/UiModel.h"
#include <string>
#include <string_view>
namespace demi::runtime::ui {
class UiLocalization {
public:
  [[nodiscard]] bool setLocale(UiDocument &document, std::string locale,
                               std::string &error) const;
  void setPseudoLocale(UiDocument &document, bool enabled) const;
private:
  static void apply(UiDocument &document, bool pseudo);
};
} // namespace demi::runtime::ui
