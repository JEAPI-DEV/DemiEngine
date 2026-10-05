#include "editor/EditorKeyBindings.h"
#include <algorithm>
#include <cctype>
#include <charconv>
#include <nlohmann/json.hpp>
#include <set>

namespace demi::editor {
namespace {
std::string lower(std::string_view text) {
  std::string result;
  for (const unsigned char character : text)
    if (!std::isspace(character))
      result.push_back(static_cast<char>(std::tolower(character)));
  return result;
}

std::string canonicalKey(std::string_view text) {
  const auto name = lower(text);
  if (name.size() == 1 && std::isalnum(static_cast<unsigned char>(name[0])))
    return std::string(1, static_cast<char>(std::toupper(name[0])));
  if (name.size() >= 2 && name[0] == 'f') {
    int number = 0;
    const auto parsed =
        std::from_chars(name.data() + 1, name.data() + name.size(), number);
    if (parsed.ec == std::errc{} && parsed.ptr == name.data() + name.size() &&
        number >= 1 && number <= 24)
      return "F" + std::to_string(number);
  }
  for (const auto key :
       {"Delete", "Backspace", "Enter", "Escape", "Tab", "Space", "Home", "End",
        "PageUp", "PageDown", "Insert", "Left", "Right", "Up", "Down"})
    if (lower(key) == name)
      return key;
  return {};
}
} // namespace

std::optional<EditorKeyChord> parseEditorKeyChord(std::string_view text,
                                                  std::string &error) {
  error.clear();
  EditorKeyChord result;
  std::size_t start = 0;
  while (start <= text.size()) {
    const auto end = text.find('+', start);
    const auto token = text.substr(start, end == text.npos ? text.size() - start
                                                           : end - start);
    const auto name = lower(token);
    unsigned modifier = 0;
    if (name == "ctrl")
      modifier = Control;
    else if (name == "shift")
      modifier = Shift;
    else if (name == "alt")
      modifier = Alt;
    else if (name == "super")
      modifier = Super;
    if (modifier) {
      if (result.modifiers & modifier) {
        error = "A shortcut repeats a modifier.";
        return {};
      }
      result.modifiers |= modifier;
    } else {
      const auto key = canonicalKey(token);
      if (key.empty() || !result.key.empty()) {
        error =
            "Use one key with optional Ctrl, Shift, Alt or Super modifiers.";
        return {};
      }
      result.key = key;
    }
    if (end == text.npos)
      break;
    start = end + 1;
  }
  if (result.key.empty()) {
    error = "A shortcut needs a non-modifier key.";
    return {};
  }
  return result;
}

std::string formatEditorKeyChord(const EditorKeyChord &chord) {
  std::string text;
  if (chord.modifiers & Control)
    text += "Ctrl+";
  if (chord.modifiers & Shift)
    text += "Shift+";
  if (chord.modifiers & Alt)
    text += "Alt+";
  if (chord.modifiers & Super)
    text += "Super+";
  return text + chord.key;
}

EditorKeyBindings::EditorKeyBindings() {
  for (const auto &definition : editorCommandDefinitions()) {
    auto &keys = bindings_[definition.command];
    for (const auto shortcut : definition.defaults) {
      std::string error;
      keys.push_back(*parseEditorKeyChord(shortcut, error));
    }
  }
}

const std::vector<EditorKeyChord> &
EditorKeyBindings::bindings(EditorCommand command) const {
  return bindings_.at(command);
}

std::string EditorKeyBindings::label(EditorCommand command) const {
  std::string text;
  for (const auto &chord : bindings(command)) {
    if (!text.empty())
      text += " / ";
    text += formatEditorKeyChord(chord);
  }
  return text;
}

bool EditorKeyBindings::assign(EditorCommand command,
                               std::vector<EditorKeyChord> chords,
                               std::string &error) {
  error.clear();
  if (editorCommandDefinition(command).required && chords.empty()) {
    error = "The Game View cursor-release action must remain bound.";
    return false;
  }
  std::set<EditorKeyChord> unique;
  for (const auto &chord : chords) {
    const auto parsed = parseEditorKeyChord(formatEditorKeyChord(chord), error);
    if (!parsed || *parsed != chord || !unique.insert(chord).second) {
      if (error.empty())
        error = "A shortcut is duplicated or invalid.";
      return false;
    }
    for (const auto &other : editorCommandDefinitions()) {
      if (other.command == command)
        continue;
      const auto &definition = editorCommandDefinition(command);
      const unsigned contexts =
          definition.contexts | (definition.global ? 16 : 0);
      const unsigned otherContexts = other.contexts | (other.global ? 16 : 0);
      const bool overlap = (contexts & otherContexts) != 0;
      if (overlap && std::ranges::find(bindings(other.command), chord) !=
                         bindings(other.command).end()) {
        error = formatEditorKeyChord(chord) + " is already assigned to " +
                std::string(other.label) + ".";
        return false;
      }
    }
  }
  bindings_[command] = std::move(chords);
  return true;
}

bool EditorKeyBindings::load(const nlohmann::json &document,
                             std::string &error) {
  if (!document.is_object()) {
    error = "Key bindings must be an object.";
    return false;
  }
  EditorKeyBindings candidate;
  try {
    // Apply overrides together: swapping two keys must not conflict with the
    // defaults they replace.
    for (const auto &[id, values] : document.items()) {
      const auto definitions = editorCommandDefinitions();
      const auto found =
          std::ranges::find(definitions, id, &EditorCommandDefinition::id);
      if (found == definitions.end() || !values.is_array()) {
        error = "Unknown command or malformed key bindings: " + id;
        return false;
      }
      candidate.bindings_[found->command].clear();
    }
    for (const auto &[id, values] : document.items()) {
      const auto definitions = editorCommandDefinitions();
      const auto found =
          std::ranges::find(definitions, id, &EditorCommandDefinition::id);
      std::vector<EditorKeyChord> keys;
      for (const auto &value : values) {
        auto chord = parseEditorKeyChord(value.get<std::string>(), error);
        if (!chord)
          return false;
        keys.push_back(std::move(*chord));
      }
      if (!candidate.assign(found->command, std::move(keys), error))
        return false;
    }
  } catch (const std::exception &failure) {
    error = failure.what();
    return false;
  }
  *this = std::move(candidate);
  return true;
}

nlohmann::json EditorKeyBindings::toJson() const {
  auto document = nlohmann::json::object();
  const EditorKeyBindings defaults;
  for (const auto &definition : editorCommandDefinitions()) {
    if (bindings(definition.command) == defaults.bindings(definition.command))
      continue;
    auto keys = nlohmann::json::array();
    for (const auto &chord : bindings(definition.command))
      keys.push_back(formatEditorKeyChord(chord));
    document[definition.id] = std::move(keys);
  }
  return document;
}
} // namespace demi::editor
