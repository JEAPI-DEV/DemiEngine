#pragma once

#include "editor/EditorAssetsPanel.h"
#include "editor/EditorConflictPanel.h"
#include "editor/EditorGameViewPanel.h"
#include "editor/EditorHierarchyPanel.h"
#include "editor/EditorPlaySession.h"
#include "editor/EditorUiHost.h"
#include "editor/EditorWorkspace.h"

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
  [[nodiscard]] EditorViewportArea gameArea() const { return gameArea_; }
  [[nodiscard]] bool gameViewFocused() const { return gameViewFocused_; }
  [[nodiscard]] bool showingGameView() const { return showGameView_; }
  [[nodiscard]] EditorPlaySession &playSession() { return playSession_; }
  [[nodiscard]] bool takeStepRequest() {
    const bool requested = stepRequested_;
    stepRequested_ = false;
    return requested;
  }
  void setGameTextureIndex(std::uint16_t value) { gameTextureIndex_ = value; }
  [[nodiscard]] bool viewportInputCaptured() const {
    if (showGameView_)
      return false;
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
  EditorViewportArea gameArea_;
  EditorAssetsPanel assetsPanel_;
  EditorHierarchyPanel hierarchyPanel_;
  EditorConflictPanel conflictPanel_;
  std::string notice_;
  std::string selectedRuntimeEntityId_;
  bool wantsExit_ = false;
  bool showGameView_ = false;
  bool gameViewFocused_ = false;
  bool stepRequested_ = false;
  std::uint16_t gameTextureIndex_ = UINT16_MAX;
  bool linuxTarget_ = true;
  bool androidTarget_ = false;
};

} // namespace demi::editor
