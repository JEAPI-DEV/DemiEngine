#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct ImVec2;

namespace demi::editor {

class EditorWorkspace;

// Presents the authored entity hierarchy and translates pointer/menu intent
// into stable-id workspace operations. It owns only transient panel state.
class EditorHierarchyPanel {
public:
  void draw(EditorWorkspace &workspace, ImVec2 position, ImVec2 size,
            bool hudOnly, std::string &notice, bool *open = nullptr);
  void requestRename(std::string id) { pendingRename_ = std::move(id); }

private:
  const EditorWorkspace *selectionWorkspace_ = nullptr;
  std::string selectionDocument_;
  std::vector<std::string> selectionIds_;
  std::uint64_t selectionRevision_ = 0;
  bool selectionIsHud_ = false;
  std::array<char, 128> filter_{};
  std::array<char, 128> rename_{};
  std::optional<std::string> renamingEntityId_;
  std::optional<std::string> pendingRename_;
};

} // namespace demi::editor
