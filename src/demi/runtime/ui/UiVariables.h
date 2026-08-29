#pragma once

#include "demi/runtime/ui/UiModel.h"

#include <string>
#include <string_view>

namespace demi::runtime::ui {

// Resolves generic authored HUD text variables. File formats, locales, and
// fallback policy belong to higher-level packages.
class UiVariables {
public:
  [[nodiscard]] bool set(UiDocument &document, std::string name,
                         std::string value, std::string &error) const;
  [[nodiscard]] bool
  setMany(UiDocument &document,
          std::unordered_map<std::string, std::string> variables,
          std::string &error) const;
  void reset(UiDocument &document) const;
  void discover(UiDocument &document) const;
  void apply(UiDocument &document) const;

  [[nodiscard]] static std::string
  resolve(std::string_view text,
          const std::unordered_map<std::string, std::string> &variables);
};

} // namespace demi::runtime::ui
