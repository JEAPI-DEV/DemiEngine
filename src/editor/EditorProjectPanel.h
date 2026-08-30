#pragma once

#include <array>
#include <string>
#include <unordered_map>

namespace demi::editor {

class EditorWorkspace;

class EditorProjectPanel {
public:
  void openSettings() { showSettings_ = true; }
  void openCreateProject() { showCreateProject_ = true; }
  void draw(EditorWorkspace &workspace, std::string &notice);

private:
  struct InputBindingEditor {
    std::array<char, 160> value{};
    std::string source;
  };

  std::array<char, 160> sceneId_{};
  std::array<char, 240> scenePath_{};
  std::array<char, 240> projectDestination_{};
  std::array<char, 128> projectName_{};
  std::array<char, 96> actionName_{};
  std::array<char, 96> actionContext_{};
  std::array<char, 128> actionBinding_{};
  std::string actionType_ = "button";
  std::string selectedTemplate_;
  std::unordered_map<std::string, InputBindingEditor> inputBindingEditors_;
  bool showSettings_ = false;
  bool showCreateProject_ = false;
};

} // namespace demi::editor
