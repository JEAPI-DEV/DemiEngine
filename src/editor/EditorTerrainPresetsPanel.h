#pragma once
#include <string>

namespace demi::editor {
class EditorWorkspace;
class EditorTerrainPresetsPanel {
public:
  // True when the graph draft was replaced and its local history must reset.
  bool draw(EditorWorkspace &workspace, std::string &notice, bool *open);

private:
  std::string selected_;
  std::string binding_;
};
} // namespace demi::editor
