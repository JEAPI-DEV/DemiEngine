#pragma once

#include "editor/EditorAssetGroupDocument.h"

#include <array>
#include <filesystem>
#include <optional>
#include <set>
#include <string>

namespace demi::editor {

class EditorWorkspace;

class EditorAssetDialogs {
public:
  void openImport() { showImport_ = true; }
  void openCreateGroup() { showCreateGroup_ = true; }
  [[nodiscard]] bool openEditGroup(const std::filesystem::path &path,
                                   std::string &error);
  void draw(EditorWorkspace &workspace, std::string &notice);

private:
  std::array<char, 240> importSource_{};
  std::array<char, 160> importId_{};
  std::array<char, 80> importType_{};
  std::array<char, 160> groupId_{};
  std::set<std::string> groupRoots_;
  std::optional<EditorAssetGroupDocument> groupDocument_;
  bool showImport_ = false;
  bool showCreateGroup_ = false;
  bool showEditGroup_ = false;
};

} // namespace demi::editor
