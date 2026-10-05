#pragma once

#include "editor/EditorCommands.h"
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>
#include <vector>

namespace demi::editor {
enum EditorKeyModifier : unsigned {
  Control = 1,
  Shift = 2,
  Alt = 4,
  Super = 8
};

struct EditorKeyChord {
  std::string key;
  unsigned modifiers = 0;
  auto operator<=>(const EditorKeyChord &) const = default;
};

std::optional<EditorKeyChord> parseEditorKeyChord(std::string_view text,
                                                  std::string &error);
std::string formatEditorKeyChord(const EditorKeyChord &chord);

class EditorKeyBindings {
public:
  EditorKeyBindings();
  const std::vector<EditorKeyChord> &bindings(EditorCommand command) const;
  std::string label(EditorCommand command) const;
  bool assign(EditorCommand command, std::vector<EditorKeyChord> chords,
              std::string &error);
  bool load(const nlohmann::json &document, std::string &error);
  nlohmann::json toJson() const;
  auto operator<=>(const EditorKeyBindings &) const = default;

private:
  std::map<EditorCommand, std::vector<EditorKeyChord>> bindings_;
};
} // namespace demi::editor
