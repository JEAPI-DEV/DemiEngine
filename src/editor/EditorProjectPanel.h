#pragma once

#include <array>
#include <string>

namespace demi::editor {

class EditorWorkspace;

class EditorProjectPanel {
public:
  void openSettings() { showSettings_ = true; }
  void openCreateProject() { showCreateProject_ = true; }
  void draw(EditorWorkspace &workspace, std::string &notice);

private:
  std::array<char, 160> sceneId_{};
  std::array<char, 240> scenePath_{};
  std::array<char, 240> projectDestination_{};
  std::array<char, 128> projectName_{};
  std::string selectedTemplate_;
  bool showSettings_ = false;
  bool showCreateProject_ = false;
};

} // namespace demi::editor
