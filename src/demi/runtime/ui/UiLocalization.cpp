#include "demi/runtime/ui/UiLocalization.h"
namespace demi::runtime::ui {
namespace {
std::string pseudo(std::string_view value) {
  std::string out = "[";
  out.reserve(value.size() * 2 + 2);
  for (const char c : value) {
    out += c;
    if (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u' ||
        c == 'A' || c == 'E' || c == 'I' || c == 'O' || c == 'U') out += c;
  }
  return out + "]";
}
}
bool UiLocalization::setLocale(UiDocument &document, std::string locale,
                               std::string &error) const {
  const auto values = document.locales.find(locale);
  if (values == document.locales.end()) {
    error = "HUD locale is not loaded: " + locale;
    return false;
  }
  document.locale = std::move(locale);
  document.localization = values->second;
  apply(document, false);
  return true;
}
void UiLocalization::setPseudoLocale(UiDocument &document, bool enabled) const {
  apply(document, enabled);
}
void UiLocalization::apply(UiDocument &document, bool usePseudo) {
  for (auto &node : document.nodes) {
    if (!node.localizationKey.empty())
      if (const auto found = document.localization.find(node.localizationKey);
          found != document.localization.end())
        node.text = usePseudo ? pseudo(found->second) : found->second;
    if (!node.placeholderLocalizationKey.empty())
      if (const auto found = document.localization.find(node.placeholderLocalizationKey);
          found != document.localization.end())
        node.placeholder = usePseudo ? pseudo(found->second) : found->second;
  }
}
} // namespace demi::runtime::ui
