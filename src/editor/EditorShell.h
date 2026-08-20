#pragma once

#include "editor/EditorConflictPanel.h"
#include "editor/EditorHierarchyPanel.h"
#include "editor/EditorPlaySession.h"
#include "editor/EditorUiHost.h"
#include "editor/EditorWorkspace.h"

#include <array>
#include <filesystem>
#include <string>

namespace demi::editor {

class EditorShell {
public:
  explicit EditorShell(EditorWorkspace &workspace);

  void draw(int width, int height, std::string_view rendererName);
  [[nodiscard]] bool wantsExit() const { return wantsExit_; }
  [[nodiscard]] EditorViewportArea viewportArea() const {
    return viewportArea_;
  }
  [[nodiscard]] bool viewportInputCaptured() const {
    return workspace_.viewDimension() ==
                   EditorSceneViewDimension::TwoDimensional
               ? workspace_.sceneView2D().capturesPointer() ||
                     workspace_.viewportTool2D().isDragging()
               : workspace_.sceneView().capturesPointer() ||
                     workspace_.viewportTool().isDragging();
  }
  void setNotice(std::string notice) { notice_ = std::move(notice); }

private:
  EditorWorkspace &workspace_;
  EditorPlaySession playSession_;
  EditorViewportArea viewportArea_;
  EditorHierarchyPanel hierarchyPanel_;
  EditorConflictPanel conflictPanel_;
  std::array<char, 128> assetFilter_{};
  std::filesystem::path selectedSource_;
  std::string notice_;
  bool wantsExit_ = false;
  bool linuxTarget_ = true;
  bool androidTarget_ = false;
};

} // namespace demi::editor
