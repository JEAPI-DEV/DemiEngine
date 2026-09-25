#pragma once

#include "editor/EditorAssetGroupDocument.h"
#include "editor/EditorSourceCreation.h"
#include "demi/assets/ColliderAssetGenerator.h"

#include <array>
#include <filesystem>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace demi::editor {

class EditorWorkspace;

class EditorAssetDialogs {
public:
  void openImport() { showImport_ = true; }
  void queueImport(std::filesystem::path source);
  void openCreateGroup() { showCreateGroup_ = true; }
  void openNewFolder(std::filesystem::path relativeParent);
  void openGenerateCollider(std::filesystem::path modelManifest,
                            std::string_view modelId);
  void openNewSource(EditorSourceKind kind) { sourceKind_=kind;sourceName_.fill(0);showNewSource_=true; }
  std::optional<std::filesystem::path> takeCreatedSource() { return std::exchange(createdSource_,std::nullopt); }
  [[nodiscard]] std::optional<std::filesystem::path> takeCreatedFolder() {
    return std::exchange(createdFolder_, std::nullopt);
  }
  [[nodiscard]] std::optional<std::filesystem::path> takeCreatedCollider() {
    return std::exchange(createdCollider_, std::nullopt);
  }
  [[nodiscard]] bool openEditGroup(const std::filesystem::path &path,
                                   std::string &error);
  void draw(EditorWorkspace &workspace, std::string &notice);

private:
  EditorSourceKind sourceKind_=EditorSourceKind::Scene3D;
  bool showNewSource_=false;
  std::array<char,192> sourceName_{};
  std::optional<std::filesystem::path> createdSource_;
  std::array<char, 256> folderName_{};
  std::filesystem::path folderParent_;
  std::optional<std::filesystem::path> createdFolder_;
  std::string folderError_;
  bool showNewFolder_ = false;
  bool focusFolderName_ = false;
  std::array<char, 240> importSource_{};
  std::array<char, 160> importId_{};
  std::array<char, 80> importType_{};
  std::filesystem::path colliderModelManifest_;
  std::array<char, 160> colliderId_{};
  std::string colliderBody_ = "static";
  float colliderDetail_ = 1.0F;
  bool showGenerateCollider_ = false;
  std::optional<assets::ColliderRecommendation> colliderRecommendation_;
  std::string colliderRecommendationBody_;
  std::string colliderRecommendationError_;
  std::optional<std::string> colliderReplacementHash_;
  std::string colliderReplacementId_;
  std::optional<std::filesystem::path> createdCollider_;
  std::array<char, 160> groupId_{};
  std::set<std::string> groupRoots_;
  std::optional<EditorAssetGroupDocument> groupDocument_;
  bool showImport_ = false;
  bool showCreateGroup_ = false;
  bool showEditGroup_ = false;
  std::vector<std::filesystem::path> droppedSources_;
};

} // namespace demi::editor
