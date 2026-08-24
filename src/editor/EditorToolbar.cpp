#include "editor/EditorToolbar.h"

#include "editor/EditorChrome.h"
#include "editor/EditorPanelStyle.h"
#include "editor/EditorPlaySession.h"
#include "editor/EditorWorkspace.h"

#include <algorithm>
#include <string_view>

namespace demi::editor {
namespace {

void sameLine() { ImGui::SameLine(0.0F, 4.0F); }

bool is2D(const EditorWorkspace &workspace) {
  return workspace.viewDimension() == EditorSceneViewDimension::TwoDimensional;
}

EditorGizmoOperation operation(const EditorWorkspace &workspace) {
  return is2D(workspace) ? workspace.viewportTool2D().operation()
                         : workspace.viewportTool().operation();
}

void setOperation(EditorWorkspace &workspace,
                  const EditorGizmoOperation value) {
  if (is2D(workspace))
    workspace.viewportTool2D().setOperation(value);
  else
    workspace.viewportTool().setOperation(value);
}

void frameSelection(EditorWorkspace &workspace) {
  if (is2D(workspace) && workspace.selectedIsoGridCell()) {
    (void)workspace.sceneView2D().frameGridCell(
        workspace.project().world, *workspace.selectedIsoGridCell());
  } else if (is2D(workspace)) {
    (void)workspace.sceneView2D().frameEntity(workspace.project().world,
                                              workspace.selectedEntityId());
  } else {
    (void)workspace.sceneView().frameEntity(workspace.project().world,
                                            workspace.selectedEntityId());
  }
}

void alignToCamera(EditorWorkspace &workspace) {
  if (is2D(workspace))
    (void)workspace.sceneView2D().alignToFirstCamera(workspace.project().world);
  else
    (void)workspace.sceneView().alignToFirstCamera(workspace.project().world);
}

void drawDocumentGroup(EditorWorkspace &workspace, std::string &notice) {
  if (editorIconButton("refresh-project", EditorIcon::Refresh,
                       "Refresh project (F5)")) {
    std::string error;
    notice = workspace.refresh(error) ? "Project refreshed" : error;
  }
  sameLine();
  if (editorIconButton("save-scene", EditorIcon::Save, "Save scene (Ctrl+S)",
                       false, workspace.activeDocumentDirty())) {
    std::string error;
    notice = workspace.save(error) ? "Document saved" : error;
  }
  editorToolbarSeparator();
  if (editorIconButton("undo-scene", EditorIcon::Undo, "Undo (Ctrl+Z)", false,
                       workspace.activeDocumentCanUndo())) {
    std::string error;
    notice = workspace.undo(error) ? "Undid scene edit" : error;
  }
  sameLine();
  if (editorIconButton("redo-scene", EditorIcon::Redo, "Redo (Ctrl+Y)", false,
                       workspace.activeDocumentCanRedo())) {
    std::string error;
    notice = workspace.redo(error) ? "Redid scene edit" : error;
  }
}

void drawPlayGroup(EditorWorkspace &workspace, EditorPlaySession &playSession,
                   bool &showGameView, bool &stepRequested,
                   std::string &notice) {
  const bool canStart = !playSession.isRunning() &&
                        playSession.state() != EditorPlayState::Starting;
  if (editorIconButton("play", EditorIcon::Play, "Play in the Game view",
                       playSession.state() == EditorPlayState::Running,
                       canStart)) {
    std::string error;
    if (workspace.hudDirty() && !workspace.saveHud(error)) {
      notice = error;
    } else if (workspace.sceneDocument().isDirty() && !workspace.save(error)) {
      notice = error;
    } else if (playSession.startEmbedded(workspace.projectPath(), error)) {
      notice = "Embedded play session started";
      showGameView = true;
    } else {
      notice = error;
    }
  }
  sameLine();
  if (editorIconButton("pause", EditorIcon::Pause,
                       playSession.isPaused() ? "Resume Play" : "Pause Play",
                       playSession.isPaused(), playSession.isRunning())) {
    std::string error;
    notice = playSession.togglePause(error)
                 ? (playSession.isPaused() ? "Play session paused"
                                           : "Play session resumed")
                 : error;
  }
  sameLine();
  if (editorIconButton("step", EditorIcon::Frame,
                       "Advance exactly one fixed tick", false,
                       playSession.isEmbedded() && playSession.isPaused()))
    stepRequested = true;
  sameLine();
  if (editorIconButton("stop", EditorIcon::Stop, "Stop Play", false,
                       playSession.isRunning())) {
    playSession.stop();
    notice = "Play session stopped";
  }
  sameLine();
  if (editorIconButton("play-options", EditorIcon::Settings, "Play options"))
    ImGui::OpenPopup("play-options-popup");
  if (ImGui::BeginPopup("play-options-popup")) {
    ImGui::BeginDisabled(playSession.isRunning());
    if (ImGui::MenuItem("Play in external window")) {
      std::string error;
      if (workspace.hudDirty() && !workspace.saveHud(error))
        notice = error;
      else if (workspace.sceneDocument().isDirty() && !workspace.save(error))
        notice = error;
      else if (playSession.startExternal(workspace.projectPath(), error))
        notice = "External play session started";
      else
        notice = error;
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
  }
}

void drawTransformGroup(EditorWorkspace &workspace) {
  if (workspace.sceneDomain() == EditorSceneDomain::Mixed) {
    if (ImGui::Button(is2D(workspace) ? "2D" : "3D", {38.0F, 30.0F}))
      workspace.setViewDimension(
          is2D(workspace) ? EditorSceneViewDimension::ThreeDimensional
                          : EditorSceneViewDimension::TwoDimensional);
    sameLine();
  } else if (!is2D(workspace)) {
    const bool perspective =
        workspace.sceneView().projection() == EditorProjection::Perspective;
    if (ImGui::Button(perspective ? "Persp" : "Ortho", {54.0F, 30.0F}))
      workspace.sceneView().setProjection(perspective
                                              ? EditorProjection::Orthographic
                                              : EditorProjection::Perspective);
    sameLine();
  }
  const EditorGizmoOperation selected = operation(workspace);
  (void)editorIconButton("select-tool", EditorIcon::Pointer,
                         "Selection is always available in the Scene view",
                         false, false);
  sameLine();
  if (editorIconButton("move-tool", EditorIcon::Move, "Move tool",
                       selected == EditorGizmoOperation::Translate))
    setOperation(workspace, EditorGizmoOperation::Translate);
  sameLine();
  if (editorIconButton("rotate-tool", EditorIcon::Rotate, "Rotate tool",
                       selected == EditorGizmoOperation::Rotate))
    setOperation(workspace, EditorGizmoOperation::Rotate);
  sameLine();
  if (editorIconButton("scale-tool", EditorIcon::Scale, "Scale tool",
                       selected == EditorGizmoOperation::Scale))
    setOperation(workspace, EditorGizmoOperation::Scale);
  sameLine();
  const bool local = is2D(workspace)
                         ? workspace.sceneView2D().transformSpace() ==
                               EditorTransformSpace::Local
                         : workspace.sceneView().transformSpace() ==
                               EditorTransformSpace::Local;
  if (ImGui::Button(local ? "Local" : "World", {57.0F, 30.0F})) {
    const EditorTransformSpace replacement =
        local ? EditorTransformSpace::World : EditorTransformSpace::Local;
    if (is2D(workspace))
      workspace.sceneView2D().setTransformSpace(replacement);
    else
      workspace.sceneView().setTransformSpace(replacement);
  }
  sameLine();
  if (editorIconButton("frame-selected", EditorIcon::Frame,
                       "Frame selected (F)"))
    frameSelection(workspace);
  sameLine();
  if (editorIconButton("align-camera", EditorIcon::Camera,
                       "Align view to the first authored camera"))
    alignToCamera(workspace);
  sameLine();
  if (editorIconButton("reset-view", EditorIcon::Refresh, "Reset Scene view")) {
    if (is2D(workspace))
      workspace.sceneView2D().reset(workspace.project().world);
    else
      workspace.sceneView().reset(workspace.project().world);
  }
}

void drawSnapAndVisibility(EditorWorkspace &workspace, const float available) {
  float &translation = is2D(workspace) ? workspace.sceneView2D().translationSnap
                                       : workspace.sceneView().translationSnap;
  float &rotation = is2D(workspace)
                        ? workspace.sceneView2D().rotationSnapDegrees
                        : workspace.sceneView().rotationSnapDegrees;
  float &scale = is2D(workspace) ? workspace.sceneView2D().scaleSnap
                                 : workspace.sceneView().scaleSnap;
  if (editorIconButton("grid-snap", EditorIcon::Grid,
                       "Position and angle snapping", true)) {
  }
  sameLine();
  ImGui::SetNextItemWidth(58.0F);
  if (ImGui::InputFloat("##translation-snap", &translation, 0.0F, 0.0F, "%.1f"))
    translation = std::clamp(translation, 0.0F, 1000.0F);
  sameLine();
  ImGui::TextDisabled("Angle");
  sameLine();
  ImGui::SetNextItemWidth(54.0F);
  if (ImGui::InputFloat("##rotation-snap", &rotation, 0.0F, 0.0F, "%.0f"))
    rotation = std::clamp(rotation, 0.0F, 180.0F);

  if (available >= 330.0F) {
    sameLine();
    ImGui::SetNextItemWidth(54.0F);
    if (ImGui::InputFloat("##scale-snap", &scale, 0.0F, 0.0F, "S %.2f"))
      scale = std::clamp(scale, 0.0F, 100.0F);
  }

  if (available < 400.0F)
    return;
  editorToolbarSeparator();
  bool &showBounds = is2D(workspace) ? workspace.sceneView2D().showBounds
                                     : workspace.sceneView().showBounds;
  bool &showCameras = is2D(workspace) ? workspace.sceneView2D().showCameras
                                      : workspace.sceneView().showCameras;
  if (editorIconButton("show-bounds", EditorIcon::Eye, "Toggle bounds",
                       showBounds))
    showBounds = !showBounds;
  sameLine();
  if (editorIconButton("show-cameras", EditorIcon::Camera, "Toggle cameras",
                       showCameras))
    showCameras = !showCameras;
  sameLine();
  if (editorIconButton("visibility-options", EditorIcon::Settings,
                       "Viewport visibility options"))
    ImGui::OpenPopup("visibility-options-popup");
  if (ImGui::BeginPopup("visibility-options-popup")) {
    bool &showColliders = is2D(workspace)
                              ? workspace.sceneView2D().showColliders
                              : workspace.sceneView().showColliders;
    ImGui::Checkbox("Bounds", &showBounds);
    ImGui::Checkbox("Colliders", &showColliders);
    ImGui::Checkbox("Cameras", &showCameras);
    if (is2D(workspace))
      ImGui::Checkbox("Grid", &workspace.sceneView2D().showGrid);
    else
      ImGui::Checkbox("Lights", &workspace.sceneView().showLights);
    ImGui::EndPopup();
  }
}

} // namespace

void drawEditorToolbar(const ImVec2 position, const ImVec2 size,
                       EditorWorkspace &workspace,
                       EditorPlaySession &playSession, bool &showGameView,
                       bool &stepRequested, std::string &notice) {
  beginEditorPanel("Toolbar", position, size, ImGuiWindowFlags_NoScrollbar);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {4.0F, 4.0F});
  drawDocumentGroup(workspace, notice);

  const float playStart = std::max(340.0F, size.x * 0.225F);
  ImGui::SameLine(playStart);
  drawPlayGroup(workspace, playSession, showGameView, stepRequested, notice);
  editorToolbarSeparator();
  drawTransformGroup(workspace);
  editorToolbarSeparator();

  const float remaining = size.x - ImGui::GetCursorPosX();
  drawSnapAndVisibility(workspace, remaining);

  const std::string_view state = editorPlayStateLabel(playSession.state());
  const float stateWidth =
      ImGui::CalcTextSize(state.data(), state.data() + state.size()).x;
  if (ImGui::GetCursorPosX() < size.x - stateWidth - 22.0F) {
    ImGui::SameLine(size.x - stateWidth - 12.0F);
    ImGui::TextDisabled("%.*s", static_cast<int>(state.size()), state.data());
  }
  ImGui::PopStyleVar();
  ImGui::End();
}

} // namespace demi::editor
