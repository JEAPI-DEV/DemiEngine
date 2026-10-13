#pragma once

#include "editor/EditorDockingState.h"

#include <filesystem>
#include <string>

struct ImVec2;

namespace demi::editor {

// Keep saved docking coordinates coherent when DPI or user zoom changes.
void installEditorLayoutScaleTracking(float &scale);
void rescaleEditorLayout(float factor);

class EditorDockingWorkspace {
public:
  explicit EditorDockingWorkspace(std::filesystem::path editorDataRoot);

  void drawDockspace(ImVec2 position, ImVec2 size);
  void drawViewMenu(bool uiPaletteAvailable = false,
                    bool terrainNodesAvailable = false);
  // Submit after all panels: first-appearing siblings must not steal an
  // explicit document-open request. Returns true when its tab is visible.
  [[nodiscard]] bool focusPanel(std::string_view windowName);
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
