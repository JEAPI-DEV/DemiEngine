#include "editor/EditorPanelStyle.h"
#include "editor/EditorShell.h"

#include <imgui.h>

namespace demi::editor {

void EditorShell::applyViewportPreferences(EditorWorkspace &document) {
  document.sceneView().translationSnap = preferences_.translationSnap;
  document.sceneView().rotationSnapDegrees = preferences_.rotationSnapDegrees;
  document.sceneView().scaleSnap = preferences_.scaleSnap;
  document.sceneView().showBounds = preferences_.showBounds3D;
  document.sceneView().showColliders = preferences_.showColliders3D;
  document.sceneView().showLights = preferences_.showLights3D;
  document.sceneView().showCameras = preferences_.showCameras3D;
  document.sceneView2D().translationSnap = preferences_.translationSnap;
  document.sceneView2D().rotationSnapDegrees = preferences_.rotationSnapDegrees;
  document.sceneView2D().scaleSnap = preferences_.scaleSnap;
  document.sceneView2D().showGrid = preferences_.showGrid2D;
  document.sceneView2D().showBounds = preferences_.showBounds2D;
  document.sceneView2D().showColliders = preferences_.showColliders2D;
  document.sceneView2D().showCameras = preferences_.showCameras2D;
}

void EditorShell::focusAuthoring(EditorDocumentSession session, bool hud) {
  if (!documents_.focus(session, notice_))
    return;
  showGameView_ = false;
  showTerrainGraphView_ = false;
  showHudView_ = hud;
  if (hud)
    workspace().activateHudDocument();
  else if (workspace().activeDocument() == EditorWorkspaceDocument::Hud)
    (void)workspace().activateSceneDocument(notice_);
}

bool EditorShell::viewportInputCaptured() const {
  if (gameViewFocused())
    return playSession_.mouseCaptured();
  for (const auto &view : authoringViews_) {
    if (!view.workspace || !view.area.width || !view.area.height)
      continue;
    const auto &document = *view.workspace;
    if (document.sceneView2D().capturesPointer() ||
        document.viewportTool2D().isDragging() ||
        document.sceneView().capturesPointer() ||
        document.viewportTool().isDragging())
      return true;
  }
  return false;
}

void EditorShell::drawDocumentViews(ImVec2 position, ImVec2 size) {
  auto &panels = dockingWorkspace_.visibility();
  for (auto &view : authoringViews_) {
    view.area = {};
    view.workspace = nullptr;
  }
  gameArea_ = {};
  gameViewFocused_ = false;
  if (!panels.game)
    showGameView_ = false;
  if (!panels.terrainGraph)
    showTerrainGraphView_ = false;
  const auto focus = [&](const char *name) {
    if (focusWindow_ == name) {
      ImGui::SetNextWindowFocus();
    }
  };
  const auto sourceView = [&](const char *name, EditorAuthoringView kind,
                              EditorDocumentSession session, bool hud,
                              bool &open) {
    auto *document = documents_.workspace(session);
    if (!open || !document || (hud && !document->hasHudDocument()))
      return;
    focus(name);
    const bool visible = beginEditorPanel(
        name, position, size, &open,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (visible) {
      if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) {
        focusAuthoring(session, hud);
      }
      auto &view = authoringViews_[static_cast<std::size_t>(kind)];
      view.workspace = document;
      drawEditorViewport(*document, {}, {}, view.texture, view.area, view.hud,
                         hud, notice_, true);
    }
    ImGui::End();
  };
  sourceView("Viewport", EditorAuthoringView::Viewport,
             EditorDocumentSession::Scene, false, panels.viewport);
  sourceView("Prefab", EditorAuthoringView::Prefab,
             EditorDocumentSession::Prefab, false, panels.prefab);
  sourceView("HUD", EditorAuthoringView::Hud, EditorDocumentSession::Hud, true,
             panels.hud);
  sourceView("Terrain Asset", EditorAuthoringView::TerrainAsset,
             EditorDocumentSession::TerrainAsset, false, panels.terrainAsset);

  if (panels.terrainGraph && graphWorkspace_ && terrainGraphPanel_.isOpen()) {
    focus("Terrain Graph");
    if (beginEditorPanel("Terrain Graph", position, size,
                         &panels.terrainGraph)) {
      const bool focused =
          ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
      if (focused) {
        for (auto session :
             {EditorDocumentSession::Scene, EditorDocumentSession::Prefab,
              EditorDocumentSession::TerrainAsset})
          if (documents_.workspace(session) == graphWorkspace_)
            focusAuthoring(session, false);
        showTerrainGraphView_ = true;
        showHudView_ = showGameView_ = false;
      }
      terrainGraphPanel_.setKeyBindings(preferences_.keyBindings);
      terrainGraphPanel_.draw(*graphWorkspace_, notice_);
    }
    ImGui::End();
  }
  if (panels.game) {
    focus("Game View");
    if (beginEditorPanel("Game View", position, size, &panels.game,
                         ImGuiWindowFlags_NoScrollbar |
                             ImGuiWindowFlags_NoScrollWithMouse)) {
      drawEditorGameView({}, {}, gameTextureIndex_, gameArea_, gameViewFocused_,
                         true);
      if (gameViewFocused_)
        showGameView_ = true;
      if (gameViewFocused_ && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
          ImGui::IsMouseHoveringRect({float(gameArea_.x), float(gameArea_.y)},
                                     {float(gameArea_.x + gameArea_.width),
                                      float(gameArea_.y + gameArea_.height)}))
        gameInputDetached_ = false;
    }
    ImGui::End();
  }
}

} // namespace demi::editor
