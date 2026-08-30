#pragma once

#include <array>
#include <string>

namespace demi::editor {

class EditorWorkspace;

// Presents external-change choices. Persistence policy remains in the scene
// document; this class owns only transient modal/input state.
class EditorConflictPanel {
public:
  void draw(EditorWorkspace &workspace, std::string &notice);

private:
  std::array<char, 512> copyPath_{};
  bool wasOpen_ = false;
};

} // namespace demi::editor
