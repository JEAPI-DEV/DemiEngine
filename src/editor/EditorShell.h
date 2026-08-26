#pragma once

#include "editor/EditorAnimationMachinePanel.h"
#include "editor/EditorAssetsPanel.h"
#include "editor/EditorBuildPanel.h"
#include "editor/EditorConflictPanel.h"
#include "editor/EditorConsolePanel.h"
#include "editor/EditorGameViewPanel.h"
#include "editor/EditorHierarchyPanel.h"
#include "editor/EditorHudNodeInspector.h"
#include "editor/EditorPlaySession.h"
#include "editor/EditorPreferencesStore.h"
#include "editor/EditorProjectPanel.h"
#include "editor/EditorSpecializedPanel.h"
#include "editor/EditorUiHost.h"
#include "editor/EditorViewportPanel.h"
#include "editor/EditorWorkspace.h"

#include <string>

namespace demi::editor {

class EditorShell {
public:
  explicit EditorShell(EditorWorkspace &workspace);

  void draw(int width, int height, std::string_view rendererName);
  [[nodiscard]] bool wantsExit() const { return wantsExit_; }
  void requestExit() { exitRequested_ = true; }
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
  void queueAssetImport(std::filesystem::path source) {
    assetsPanel_.queueImport(std::move(source));
  }
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
  [[nodiscard]] bool openDocument(const std::filesystem::path &path,
                                  std::string &error);

private:
  EditorWorkspace &workspace_;
  EditorPlaySession playSession_;
  EditorViewportArea viewportArea_;
  EditorHudViewportState hudViewportState_;
  EditorHudInspectorState hudInspectorState_;
  EditorViewportArea gameArea_;
  EditorAssetsPanel assetsPanel_;
  EditorAnimationMachinePanel animationMachinePanel_;
  EditorBuildPanel buildPanel_;
  EditorProjectPanel projectPanel_;
  EditorSpecializedPanel specializedPanel_;
  EditorHierarchyPanel hierarchyPanel_;
  EditorConflictPanel conflictPanel_;
  EditorConsolePanel consolePanel_;
  EditorRecoveryStore recoveryStore_;
  EditorPreferencesStore preferencesStore_;
  EditorPreferences preferences_;
  std::optional<EditorRecoverySnapshot> pendingRecovery_;
  std::string recoveryFingerprint_;
  std::string notice_;
  std::string selectedRuntimeEntityId_;
  bool wantsExit_ = false;
  bool exitRequested_ = false;
  bool recoveryPromptOpened_ = false;
  bool recoverySyncBlocked_ = false;
  bool preferenceSyncBlocked_ = false;
  bool showGameView_ = false;
  bool gameViewFocused_ = false;
  bool stepRequested_ = false;
  std::uint16_t gameTextureIndex_ = UINT16_MAX;
};

} // namespace demi::editor
