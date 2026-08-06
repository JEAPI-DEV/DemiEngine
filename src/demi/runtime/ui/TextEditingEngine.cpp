#include "demi/runtime/ui/TextEditingEngine.h"

#include "demi/runtime/ui/TextLayoutEngine.h"

#include <algorithm>

namespace demi::runtime::ui {
namespace {

std::string slice(const std::string_view text, const std::size_t first,
                  const std::size_t count) {
  return TextLayoutEngine::graphemeSlice(text, first, count)
      .value_or(std::string{});
}

bool replaceSelection(std::string &text, TextEditState &state,
                      const std::string_view replacement) {
  if (!text.empty() && TextLayoutEngine::graphemeCount(text) == 0)
    return false;
  TextEditingEngine::normalize(text, state);
  const std::size_t replacementCount =
      TextLayoutEngine::graphemeCount(replacement);
  if (!replacement.empty() && replacementCount == 0)
    return false;
  const TextEditRange selected = TextEditingEngine::selection(state);
  const std::size_t total = TextLayoutEngine::graphemeCount(text);
  text = slice(text, 0, selected.first) + std::string(replacement) +
         slice(text, selected.first + selected.count,
               total - selected.first - selected.count);
  state.caret = selected.first + replacementCount;
  state.anchor = state.caret;
  state.initialized = true;
  TextEditingEngine::clearComposition(state);
  return true;
}

} // namespace

void TextEditingEngine::normalize(const std::string_view text,
                                  TextEditState &state) {
  const std::size_t count = TextLayoutEngine::graphemeCount(text);
  if (!state.initialized) {
    state.caret = count;
    state.anchor = count;
    state.initialized = true;
  } else {
    state.caret = std::min(state.caret, count);
    state.anchor = std::min(state.anchor, count);
  }
}

TextEditRange TextEditingEngine::selection(const TextEditState &state) {
  const std::size_t first = std::min(state.caret, state.anchor);
  return {.first = first,
          .count = std::max(state.caret, state.anchor) - first};
}

bool TextEditingEngine::insert(std::string &text, TextEditState &state,
                               const std::string_view utf8) {
  if (utf8.empty()) {
    clearComposition(state);
    return false;
  }
  return replaceSelection(text, state, utf8);
}

bool TextEditingEngine::backspace(std::string &text, TextEditState &state) {
  normalize(text, state);
  if (selection(state).count == 0) {
    if (state.caret == 0)
      return false;
    state.anchor = state.caret - 1;
  }
  return replaceSelection(text, state, {});
}

bool TextEditingEngine::deleteForward(std::string &text,
                                      TextEditState &state) {
  normalize(text, state);
  if (selection(state).count == 0) {
    if (state.caret >= TextLayoutEngine::graphemeCount(text))
      return false;
    state.anchor = state.caret + 1;
  }
  return replaceSelection(text, state, {});
}

void TextEditingEngine::move(TextEditState &state, const std::string_view text,
                             const int delta, const bool extendSelection) {
  normalize(text, state);
  const std::size_t count = TextLayoutEngine::graphemeCount(text);
  std::size_t target = state.caret;
  const TextEditRange selected = selection(state);
  if (!extendSelection && selected.count > 0 && delta != 0) {
    target = delta < 0 ? selected.first : selected.first + selected.count;
  } else if (delta < 0) {
    const std::size_t amount = static_cast<std::size_t>(-(delta + 1)) + 1U;
    target = amount > target ? 0 : target - amount;
  } else {
    target = std::min(count, target + static_cast<std::size_t>(delta));
  }
  moveTo(state, text, target, extendSelection);
}

void TextEditingEngine::moveTo(TextEditState &state,
                               const std::string_view text,
                               const std::size_t grapheme,
                               const bool extendSelection) {
  normalize(text, state);
  state.caret = std::min(grapheme, TextLayoutEngine::graphemeCount(text));
  if (!extendSelection)
    state.anchor = state.caret;
  clearComposition(state);
}

void TextEditingEngine::selectAll(TextEditState &state,
                                  const std::string_view text) {
  state.anchor = 0;
  state.caret = TextLayoutEngine::graphemeCount(text);
  state.initialized = true;
  clearComposition(state);
}

bool TextEditingEngine::setComposition(TextEditState &state, std::string utf8,
                                       const std::size_t selectionStart,
                                       const std::size_t selectionLength) {
  if (!utf8.empty() && TextLayoutEngine::graphemeCount(utf8) == 0)
    return false;
  const std::size_t count = TextLayoutEngine::graphemeCount(utf8);
  state.composition = std::move(utf8);
  state.compositionSelectionStart = std::min(selectionStart, count);
  state.compositionSelectionLength =
      std::min(selectionLength, count - state.compositionSelectionStart);
  return true;
}

void TextEditingEngine::clearComposition(TextEditState &state) {
  state.composition.clear();
  state.compositionSelectionStart = 0;
  state.compositionSelectionLength = 0;
}

std::string TextEditingEngine::displayText(const std::string_view text,
                                           const TextEditState &state) {
  if (state.composition.empty())
    return std::string(text);
  const std::size_t total = TextLayoutEngine::graphemeCount(text);
  const TextEditRange selected = selection(state);
  const std::size_t first = std::min(selected.first, total);
  const std::size_t removed = std::min(selected.count, total - first);
  return slice(text, 0, first) + state.composition +
         slice(text, first + removed, total - first - removed);
}

std::size_t TextEditingEngine::displayCaret(const std::string_view text,
                                            const TextEditState &state) {
  const std::size_t total = TextLayoutEngine::graphemeCount(text);
  const std::size_t first = std::min(selection(state).first, total);
  return state.composition.empty()
             ? std::min(state.caret, total)
             : first + state.compositionSelectionStart +
                   state.compositionSelectionLength;
}

} // namespace demi::runtime::ui
