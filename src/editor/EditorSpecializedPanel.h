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
    return active_ && active_->document().isDirty();
  }
  [[nodiscard]] std::optional<EditorRecoveryDocument> recoveryDocument() const;
  [[nodiscard]] bool saveActive(EditorWorkspace &workspace, std::string &error);
  [[nodiscard]] bool restore(const EditorRecoveryDocument &document,
                             EditorWorkspace &workspace, std::string &error);
  void discardActive() { active_.reset(); }

private:
  std::optional<EditorSpecializedDocument> active_;
  std::string selectedPointer_;
  std::array<char, 64> locale_{};
  std::array<char, 1024> editBuffer_{};
  std::array<char, 160> hudSampleText_{};
  std::string editBufferPointer_;
  runtime::ui::Insets safeArea_{};
  float hudDpiScale_ = 1.0F;
};

} // namespace demi::editor
