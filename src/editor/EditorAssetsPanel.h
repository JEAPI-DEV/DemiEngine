#pragma once

#include "editor/EditorAssetDialogs.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

struct ImVec2;

namespace demi::editor {

class EditorWorkspace;

class EditorAssetsPanel {
public:
  void draw(EditorWorkspace &workspace, ImVec2 position, ImVec2 size,
            std::string &notice);
  void queueImport(std::filesystem::path source) {
    dialogs_.queueImport(std::move(source));
  }
  [[nodiscard]] std::optional<std::filesystem::path> takeOpenRequest() {
    return std::exchange(openRequest_, std::nullopt);
  }

private:
  std::array<char, 128> filter_{};
  std::filesystem::path directory_;
  std::filesystem::path selectedSource_;
  std::string typeFilter_;
  EditorAssetDialogs dialogs_;
  std::optional<std::filesystem::path> openRequest_;
};

} // namespace demi::editor
