#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace demi::runtime::ui {

struct TextEditState {
  std::size_t caret = 0;
  std::size_t anchor = 0;
  std::string composition;
  std::size_t compositionSelectionStart = 0;
  std::size_t compositionSelectionLength = 0;
  bool initialized = false;
};

struct TextEditRange {
  std::size_t first = 0;
  std::size_t count = 0;
};

// Grapheme-indexed text editing policy. Platform adapters only provide
// committed UTF-8 and IME composition updates; this module owns selection and
// mutation semantics without depending on SDL, Lua, or rendering.
class TextEditingEngine {
public:
  static void normalize(std::string_view text, TextEditState &state);
  [[nodiscard]] static TextEditRange selection(const TextEditState &state);
  [[nodiscard]] static bool insert(std::string &text, TextEditState &state,
                                   std::string_view utf8);
  [[nodiscard]] static bool backspace(std::string &text,
                                      TextEditState &state);
  [[nodiscard]] static bool deleteForward(std::string &text,
                                          TextEditState &state);
  static void move(TextEditState &state, std::string_view text, int delta,
                   bool extendSelection = false);
  static void moveTo(TextEditState &state, std::string_view text,
                     std::size_t grapheme, bool extendSelection = false);
  static void selectAll(TextEditState &state, std::string_view text);
  [[nodiscard]] static bool setComposition(TextEditState &state,
                                           std::string utf8,
                                           std::size_t selectionStart,
                                           std::size_t selectionLength);
  static void clearComposition(TextEditState &state);
  [[nodiscard]] static std::string displayText(std::string_view text,
                                               const TextEditState &state);
  [[nodiscard]] static std::size_t displayCaret(std::string_view text,
                                                const TextEditState &state);
};

} // namespace demi::runtime::ui
