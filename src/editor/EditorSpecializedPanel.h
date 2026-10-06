#pragma once

#include "editor/EditorRecoveryStore.h"
#include "editor/EditorSpecializedDocument.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>

namespace demi::editor {

class EditorWorkspace;

class EditorSpecializedPanel {
public:
  [[nodiscard]] bool open(const std::filesystem::path &source,
                          const EditorAssetIndex &assets, std::string &error);
  void draw(EditorWorkspace &workspace, std::string &notice);
  [[nodiscard]] bool isDirty() const {
    return active_ && (active_->document().isDirty() || assetReimportPending_);
  }
  [[nodiscard]] std::optional<EditorRecoveryDocument> recoveryDocument() const;
  [[nodiscard]] bool saveActive(EditorWorkspace &workspace, std::string &error);
  [[nodiscard]] bool restore(const EditorRecoveryDocument &document,
                             EditorWorkspace &workspace, std::string &error);
  void discardActive() {
    active_.reset();
    assetReimportPending_ = false;
  }

private:
  std::optional<EditorSpecializedDocument> active_;
  bool assetReimportPending_ = false;
  std::string selectedPointer_;
  std::array<char, 1024> editBuffer_{};
  std::string editBufferPointer_;
};

} // namespace demi::editor
