#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

struct ImVec2;

namespace demi::editor {

class EditorWorkspace;

// Presents the authored entity hierarchy and translates pointer/menu intent
// into stable-id workspace operations. It owns only transient panel state.
class EditorHierarchyPanel {
public:
  void draw(EditorWorkspace &workspace, ImVec2 position, ImVec2 size,
            bool hudOnly, std::string &notice);

private:
  std::array<char, 128> filter_{};
  std::array<char, 128> rename_{};
  std::optional<std::string> renamingEntityId_;
};

} // namespace demi::editor
