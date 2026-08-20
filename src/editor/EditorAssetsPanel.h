#pragma once

#include <array>
#include <filesystem>
#include <string>

struct ImVec2;

namespace demi::editor {

class EditorWorkspace;

class EditorAssetsPanel {
public:
  void draw(EditorWorkspace &workspace, ImVec2 position, ImVec2 size,
            std::string &notice);

private:
  std::array<char, 128> filter_{};
  std::filesystem::path directory_;
  std::filesystem::path selectedSource_;
};

} // namespace demi::editor
