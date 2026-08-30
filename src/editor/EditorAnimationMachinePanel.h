#pragma once

#include <array>
#include <string>

namespace demi::editor {

class EditorWorkspace;

class EditorAnimationMachinePanel {
public:
  [[nodiscard]] bool canOpen(const EditorWorkspace &workspace) const;
  void open(const EditorWorkspace &workspace);
  void draw(EditorWorkspace &workspace, std::string &notice);

private:
  std::array<char, 96> stateName_{};
  std::array<char, 128> modelClip_{};
  std::array<char, 96> transitionFrom_{};
  std::array<char, 96> transitionTo_{};
  std::string entityId_;
  float duration_ = 1.0F;
  bool loop_ = true;
};

} // namespace demi::editor
