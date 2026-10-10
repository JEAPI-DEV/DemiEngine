#pragma once

#include "editor/EditorAboutPanel.h"
#include "editor/EditorAnimationMachinePanel.h"
#include "editor/EditorAssetsPanel.h"
#include "editor/EditorBuildPanel.h"
#include "editor/EditorRunPanel.h"
#include "editor/EditorConflictPanel.h"
#include "editor/EditorConsolePanel.h"
#include "editor/EditorDockingWorkspace.h"
#include "editor/EditorDocumentSessions.h"
#include "editor/EditorGameViewPanel.h"
#include "editor/EditorHierarchyPanel.h"
#include "editor/EditorHudNodeInspector.h"
#include "editor/EditorInspectorPanel.h"
#include "editor/EditorModulesPanel.h"
#include "editor/EditorPlaySession.h"
#include "editor/EditorPreferencesStore.h"
#include "editor/EditorProjectPanel.h"
#include "editor/EditorShortcutSettings.h"
#include "editor/EditorSpecializedPanel.h"
#include "editor/EditorTerrainGraphPanel.h"
#include "editor/EditorTerrainPresetsPanel.h"
#include "editor/EditorUiHost.h"
#include "editor/EditorViewportPanel.h"
#include "editor/EditorWorkspace.h"

#include <array>
#include <string>

namespace demi::editor {

struct EditorAuthoringViewState {
  EditorWorkspace *workspace = nullptr;
  EditorViewportArea area;
  EditorHudViewportState hud;
  std::uint16_t texture = UINT16_MAX;
};

class EditorShell {
public:
  explicit EditorShell(EditorWorkspace &workspace);

  void draw(int width, int height, std::string_view rendererName);
  // Release ImGui-dependent panel state before the UI host destroys ImGui.
  void releaseUiResources() noexcept {
    runPanel_.stop();
    terrainGraphPanel_.releaseUiResources();
  }
  [[nodiscard]] float uiScale() const { return uiScale_; }
  [[nodiscard]] bool wantsExit() const { return wantsExit_; }
  void requestExit() { exitRequested_ = true; }
  [[nodiscard]] const auto &authoringViews() const { return authoringViews_; }
  [[nodiscard]] EditorViewportArea gameArea() const { return gameArea_; }
  [[nodiscard]] bool gameViewFocused() const {
    return gameViewFocused_ && !gameInputDetached_;
  }
  [[nodiscard]] bool showingGameView() const { return showGameView_; }
  [[nodiscard]] bool showingHudView() const { return showHudView_; }
  [[nodiscard]] EditorPlaySession &playSession() { return playSession_; }
  [[nodiscard]] bool takeStepRequest() {
    const bool requested = stepRequested_;
    stepRequested_ = false;
    return requested;
  }
  void setGameTextureIndex(std::uint16_t value) { gameTextureIndex_ = value; }
  void setViewportTextureIndex(EditorAuthoringView view, std::uint16_t value) {
    authoringViews_[static_cast<std::size_t>(view)].texture = value;
  }
  void setBrandingTextureIndex(std::uint16_t value) {
    brandingTextureIndex_ = value;
  }
  void queueAssetImport(std::filesystem::path source) {
    assetsPanel_.queueImport(std::move(source));
  }
  [[nodiscard]] bool viewportInputCaptured() const;
  void setNotice(std::string notice) { notice_ = std::move(notice); }
  [[nodiscard]] bool openDocument(const std::filesystem::path &path,
                                  std::string &error);
  [[nodiscard]] bool openTerrainGraph(std::string &error);
  [[nodiscard]] bool openTerrainNodeSettings(std::string_view nodeId,
                                             std::string &error);

private:
  EditorWorkspace &workspace() { return documents_.focused(); }
  const EditorWorkspace &workspace() const { return documents_.focused(); }
  void drawDocumentViews(ImVec2 position, ImVec2 size);
  void focusAuthoring(EditorDocumentSession session, bool hud);
  void applyViewportPreferences(EditorWorkspace &document);
  EditorCommandContext commandContext() const;
  void executeCommand(EditorCommand command, EditorCommandContext context);
  void dispatchShortcuts();
  EditorCommandContext lastAuthoringContext_ = EditorCommandContext::Scene;
  EditorShortcutSettingsState shortcutSettings_;
  bool gameInputDetached_ = false;
  std::string documentOpenError_;
  EditorDocumentSessions documents_;
  EditorWorkspace *graphWorkspace_ = nullptr;
  std::string focusWindow_;
  std::array<EditorAuthoringViewState, EditorAuthoringViews.size()>
      authoringViews_;
  EditorDockingWorkspace dockingWorkspace_;
  EditorPlaySession playSession_;
  EditorHudInspectorState hudInspectorState_;
  EditorInspectorPanelState inspectorState_;
  EditorModulesPanelState modulesState_;
  EditorViewportArea gameArea_;
  EditorAssetsPanel assetsPanel_;
  EditorAboutPanel aboutPanel_;
  EditorAnimationMachinePanel animationMachinePanel_;
  EditorTerrainGraphPanel terrainGraphPanel_;
  EditorTerrainPresetsPanel terrainPresetsPanel_;
  EditorBuildPanel buildPanel_;
  EditorRunPanel runPanel_;
  EditorProjectPanel projectPanel_;
  EditorSpecializedPanel specializedPanel_;
  EditorHierarchyPanel hierarchyPanel_;
  EditorConflictPanel conflictPanel_;
  EditorConsolePanel consolePanel_;
  EditorRecoveryStore recoveryStore_;
  EditorPreferencesStore preferencesStore_;
  EditorPreferences preferences_;
  EditorPreferences persistedPreferences_;
  float uiScale_ = 1.0F;
  bool showSettings_ = false;
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
  bool showHudView_ = false;
  bool showTerrainGraphView_ = false;
  bool gameViewFocused_ = false;
  bool stepRequested_ = false;
  std::uint16_t gameTextureIndex_ = UINT16_MAX;
  std::uint16_t brandingTextureIndex_ = UINT16_MAX;
};

} // namespace demi::editor
