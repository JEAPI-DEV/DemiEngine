#pragma once

#include "editor/EditorDockingState.h"

#include <filesystem>
#include <string>

struct ImVec2;

namespace demi::editor {

class EditorDockingWorkspace {
public:
  explicit EditorDockingWorkspace(std::filesystem::path editorDataRoot);

  void drawDockspace(ImVec2 position, ImVec2 size);
  void drawViewMenu();
  void requestReset();
  void persistVisibilityIfChanged();

  [[nodiscard]] EditorPanelVisibility &visibility() noexcept;
  [[nodiscard]] std::string takeDiagnostic();

private:
  void buildDefaultLayout(unsigned int dockspaceId, ImVec2 size);

  EditorDockingStateStore store_;
  EditorPanelVisibility visibility_;
  EditorPanelVisibility savedVisibility_;
  std::string diagnostic_;
  bool resetRequested_ = false;
};

} // namespace demi::editor
