#include "editor/EditorToolbar.h"
#include "editor/EditorKeyBindings.h"
#include "editor/EditorRunPanel.h"

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

void alignToCamera(EditorWorkspace &workspace) {
  if (is2D(workspace))
    (void)workspace.sceneView2D().alignToFirstCamera(workspace.project().world);
  else
    (void)workspace.sceneView().alignToFirstCamera(workspace.project().world);
}

void drawDocumentGroup(EditorWorkspace &workspace,
                       const EditorKeyBindings &bindings,
                       EditorCommandContext context,
                       const std::function<void(EditorCommand)> &execute) {
  const auto tooltip = [&](EditorCommand command) {
    const auto shortcut = bindings.label(command);
    return std::string(editorCommandDefinition(command).label) +
           (shortcut.empty() ? " (unbound)" : " (" + shortcut + ")");
  };
  if (editorIconButton("refresh-project", EditorIcon::Refresh,
                       tooltip(EditorCommand::Refresh).c_str()))
    execute(EditorCommand::Refresh);
  sameLine();
  if (editorIconButton("save-scene", EditorIcon::Save,
                       tooltip(EditorCommand::SaveAll).c_str()))
    execute(EditorCommand::SaveAll);
  editorToolbarSeparator();
  if (editorIconButton("undo-scene", EditorIcon::Undo,
                       tooltip(EditorCommand::Undo).c_str(), false,
                       context == EditorCommandContext::TerrainGraph ||
                           workspace.activeDocumentCanUndo()))
    execute(EditorCommand::Undo);
  sameLine();
  if (editorIconButton("redo-scene", EditorIcon::Redo,
                       tooltip(EditorCommand::Redo).c_str(), false,
                       context == EditorCommandContext::TerrainGraph ||
                           workspace.activeDocumentCanRedo()))
    execute(EditorCommand::Redo);
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
    if (!workspace.terrainReady(error)) {
      notice = error;
    } else if (workspace.hudDirty() && !workspace.saveHud(error)) {
      notice = error;
    } else if (workspace.sceneDocument().isDirty() && !workspace.save(error)) {
      notice = error;
    } else if (playSession.startEmbedded(
                   workspace.projectPath(), error,
                   workspace.isPrefabDocument()
                       ? std::string{}
                       : workspace.sceneDocument().json().value(
                             "id", std::string{}))) {
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
}

void drawTransformGroup(EditorWorkspace &workspace,
                        const EditorKeyBindings &bindings,
                        const std::function<void(EditorCommand)> &execute,
                        bool allowCameraEdit, std::string &notice) {
  const EditorGizmoOperation selected = operation(workspace);
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
  const auto frameTooltip =
      "Frame selected (" + bindings.label(EditorCommand::FrameSelection) + ")";
  if (editorIconButton("frame-selected", EditorIcon::Frame,
                       frameTooltip.c_str()))
    execute(EditorCommand::FrameSelection);
  sameLine();
  if (ImGui::Button("View"))
    ImGui::OpenPopup("view-settings");
  if (ImGui::BeginPopup("view-settings")) {
    if (!is2D(workspace)) {
      const auto projection = workspace.sceneView().projection();
      if (ImGui::MenuItem("Perspective", nullptr,
                          projection == EditorProjection::Perspective))
        workspace.sceneView().setProjection(EditorProjection::Perspective);
      if (ImGui::MenuItem("Orthographic", nullptr,
                          projection == EditorProjection::Orthographic))
        workspace.sceneView().setProjection(EditorProjection::Orthographic);
      ImGui::Separator();
      ImGui::Checkbox("Studio preview lighting",
                      &workspace.sceneView().studioLighting);
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Neutral inspection light for this view only. "
                          "Disable to see authored scene lights.");
    }
    if (workspace.sceneDomain() == EditorSceneDomain::Mixed &&
        ImGui::MenuItem(is2D(workspace) ? "Switch to 3D" : "Switch to 2D"))
      workspace.setViewDimension(
          is2D(workspace) ? EditorSceneViewDimension::ThreeDimensional
                          : EditorSceneViewDimension::TwoDimensional);
    ImGui::Separator();
    if (ImGui::MenuItem("View through scene camera"))
      alignToCamera(workspace);
    if (ImGui::MenuItem("Align selected camera to view", nullptr, false,
                        allowCameraEdit &&
                            workspace.canAlignSelectedCameraToView())) {
      std::string error;
      notice = workspace.alignSelectedCameraToView(error)
                   ? "Camera aligned to view"
                   : error;
    }
    if (ImGui::MenuItem("Reset view")) {
      if (is2D(workspace))
        workspace.sceneView2D().reset(workspace.project().world);
      else
        workspace.sceneView().reset(workspace.project().world);
    }
    ImGui::Separator();
    ImGui::TextDisabled("Navigation");
    ImGui::TextUnformatted(
        "Orbit: Alt + left drag\nPan: middle drag\nZoom: wheel\nFly: right "
        "drag + WASDQE\nFrame selection: F");
    ImGui::EndPopup();
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
  (void)available;
  if (ImGui::Button("Snapping"))
    ImGui::OpenPopup("snap-settings");
  if (ImGui::BeginPopup("snap-settings")) {
    ImGui::TextDisabled("Zero disables snapping for that operation.");
    ImGui::InputFloat("Move step", &translation, 0, 0, "%.3f");
    ImGui::InputFloat("Rotation step (degrees)", &rotation, 0, 0, "%.1f");
    ImGui::InputFloat("Scale step", &scale, 0, 0, "%.3f");
    translation = std::clamp(translation, 0.0F, 1000.0F);
    rotation = std::clamp(rotation, 0.0F, 180.0F);
    scale = std::clamp(scale, 0.0F, 100.0F);
    ImGui::TextDisabled("Hold Shift while dragging to bypass snapping.");
    ImGui::EndPopup();
  }
  sameLine();
  if (ImGui::Button("Overlays"))
    ImGui::OpenPopup("visibility-options-popup");
  if (ImGui::BeginPopup("visibility-options-popup")) {
    ImGui::Checkbox("Bounds", is2D(workspace)
                                  ? &workspace.sceneView2D().showBounds
                                  : &workspace.sceneView().showBounds);
    ImGui::Checkbox("Colliders", is2D(workspace)
                                     ? &workspace.sceneView2D().showColliders
                                     : &workspace.sceneView().showColliders);
    ImGui::Checkbox("Cameras", is2D(workspace)
                                   ? &workspace.sceneView2D().showCameras
                                   : &workspace.sceneView().showCameras);
    if (is2D(workspace))
      ImGui::Checkbox("Grid", &workspace.sceneView2D().showGrid);
    else {
      ImGui::Checkbox("Light icons", &workspace.sceneView().showLights);
      ImGui::Checkbox("Light ranges", &workspace.sceneView().showLightRanges);
    }
    ImGui::EndPopup();
  }
}

} // namespace

void drawEditorToolbar(const ImVec2 position, const ImVec2 size,
                       EditorWorkspace &workspace,
                       EditorWorkspace &playWorkspace,
                       EditorPlaySession &playSession, EditorRunPanel &runPanel,
                       bool &showGameView, bool &stepRequested,
                       std::string &notice, const EditorKeyBindings &bindings,
                       EditorCommandContext context,
                       const std::function<void(EditorCommand)> &execute) {
  beginEditorShellPanel("Toolbar", position, size,
                        ImGuiWindowFlags_NoScrollbar);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {4.0F, 4.0F});
  drawDocumentGroup(workspace, bindings, context, execute);

  const float playStart = std::max(200.0F, size.x * 0.14F);
  ImGui::SameLine(playStart);
  ImGui::BeginDisabled(runPanel.running());
  drawPlayGroup(playWorkspace, playSession, showGameView, stepRequested,
                notice);
  ImGui::EndDisabled();
  sameLine();
  if (ImGui::Button("Run & Test"))
    runPanel.open();
  editorToolbarSeparator();
  drawTransformGroup(workspace, bindings, execute,
                     !showGameView && !playSession.isRunning() &&
                         playSession.state() != EditorPlayState::Starting,
                     notice);
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
