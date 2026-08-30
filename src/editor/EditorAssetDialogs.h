#pragma once

#include "editor/EditorAssetGroupDocument.h"

#include <array>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace demi::editor {

class EditorWorkspace;

class EditorAssetDialogs {
public:
  void openImport() { showImport_ = true; }
  void queueImport(std::filesystem::path source);
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
  std::vector<std::filesystem::path> droppedSources_;
};

} // namespace demi::editor
