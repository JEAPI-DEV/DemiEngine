#pragma once

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
