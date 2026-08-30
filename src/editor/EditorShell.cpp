#include "editor/EditorShell.h"

#include "editor/EditorChrome.h"
#include "editor/EditorHudNodeInspector.h"
#include "editor/EditorInspectorPanel.h"
#include "editor/EditorPanelStyle.h"
#include "editor/EditorToolbar.h"
#include "editor/EditorViewportPanel.h"
#include "editor/EditorWorkspaceLayout.h"

#include "demi/core/Version.h"
#include "demi/filesystem/ProjectPaths.h"
#include "demi/schema/Validation.h"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace demi::editor {
namespace {

void drawStageTabs(const ImVec2 position, const ImVec2 size,
                   bool &showGameView) {
  beginEditorPanel("StageTabs", position, size,
                   ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse);
  if (editorStageTab("Viewport", !showGameView))
    showGameView = false;
  ImGui::SameLine(0.0F, 2.0F);
  if (editorStageTab("Game View", showGameView))
    showGameView = true;
  ImGui::SameLine(size.x - 22.0F);
  ImGui::TextDisabled("x");
  ImGui::End();
}

void drawMenu(EditorWorkspace &workspace, const ImVec2 size,
              bool &exitRequested, EditorProjectPanel &projectPanel,
              EditorAnimationMachinePanel &animationPanel,
              EditorBuildPanel &buildPanel, EditorAboutPanel &aboutPanel,
              std::string &notice) {
  beginEditorPanel("MainMenu", {0.0F, 0.0F}, size,
                   ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar);
  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New project..."))
        projectPanel.openCreateProject();
      if (ImGui::MenuItem("Project settings..."))
        projectPanel.openSettings();
      ImGui::Separator();
      if (ImGui::MenuItem("Save active document", "Ctrl+S", false,
                          workspace.activeDocumentDirty())) {
        std::string error;
        notice = workspace.save(error) ? "Document saved" : error;
      }
      if (ImGui::MenuItem("Save project", nullptr, false,
                          workspace.projectDocument().isDirty())) {
        std::string error;
        notice = workspace.saveProject(error) ? "Project saved" : error;
      }
      if (ImGui::MenuItem("Refresh project", "F5")) {
        std::string error;
        notice = workspace.refresh(error) ? "Project refreshed" : error;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Exit"))
        exitRequested = true;
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo", "Ctrl+Z", false,
                          workspace.activeDocumentCanUndo())) {
        std::string error;
        notice = workspace.undo(error) ? "Undid scene edit" : error;
      }
      if (ImGui::MenuItem("Redo", "Ctrl+Y", false,
                          workspace.activeDocumentCanRedo())) {
        std::string error;
        notice = workspace.redo(error) ? "Redid scene edit" : error;
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      ImGui::MenuItem("Reset workspace", nullptr, false, false);
      ImGui::EndMenu();
    }
    ImGui::MenuItem("Scene");
    if (ImGui::BeginMenu("Tools")) {
      const bool canEditAnimation = animationPanel.canOpen(workspace);
      if (ImGui::MenuItem("Animation State Machine...", nullptr, false,
                          canEditAnimation))
        animationPanel.open(workspace);
      if (!canEditAnimation &&
          ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(
            "Select an entity with Animation State Machine first.");
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Build")) {
      if (ImGui::MenuItem("Build Project..."))
        buildPanel.open();
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("About..."))
        aboutPanel.open();
      ImGui::EndMenu();
    }
    ImGui::EndMenuBar();
  }
  ImGui::End();
}

void drawStatus(EditorWorkspace &workspace, const ImVec2 position,
                const ImVec2 size, const std::string_view renderer,
                const std::string &notice, const bool linuxTarget,
                const bool androidTarget) {
  beginEditorPanel("Status", position, size,
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  draw->AddCircleFilled({cursor.x + 3.0F, cursor.y + 7.0F}, 4.0F, EditorAccent);
  ImGui::SetCursorPosX(20.0F);
  ImGui::Text("%s %s", EngineName.data(), EngineVersion.data());
  ImGui::SameLine(190.0F);
  ImGui::TextDisabled("Lua Scripting");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Renderer: %.*s", static_cast<int>(renderer.size()),
                      renderer.data());
  if (!notice.empty()) {
    ImGui::SameLine(360.0F);
    ImGui::TextDisabled("%s", notice.c_str());
  }
  const std::string project = "Project: " + workspace.project().project.name;
  const std::string target = linuxTarget && androidTarget
                                 ? "Target: Linux, Android"
                             : linuxTarget   ? "Target: Linux"
                             : androidTarget ? "Target: Android"
                                             : "No target";
  ImGui::SameLine(std::max(size.x - 520.0F, 420.0F));
  ImGui::TextDisabled("%s", project.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("|");
  ImGui::SameLine();
  ImGui::TextDisabled("%s", target.c_str());
  ImGui::SameLine(size.x - 105.0F);
  if (workspace.sceneDocument().isDirty())
    ImGui::TextColored({0.95F, 0.67F, 0.28F, 1.0F}, "Modified");
  else
    ImGui::TextColored({0.32F, 0.86F, 0.49F, 1.0F}, "Ready");
  ImGui::End();
}

} // namespace

EditorShell::EditorShell(EditorWorkspace &workspace)
    : workspace_(workspace), recoveryStore_(defaultEditorCacheDirectory()),
      preferencesStore_(defaultEditorDataDirectory()) {
  std::string error;
  pendingRecovery_ = recoveryStore_.load(workspace_.projectPath(), error);
  if (!error.empty()) {
    notice_ = "Recovery cache: " + error;
    recoverySyncBlocked_ = true;
  }
  error.clear();
  if (!preferencesStore_.load(preferences_, error)) {
    notice_ = "Editor preferences: " + error;
    preferenceSyncBlocked_ = true;
  }
  workspace_.sceneView().translationSnap = preferences_.translationSnap;
  workspace_.sceneView().rotationSnapDegrees = preferences_.rotationSnapDegrees;
  workspace_.sceneView().scaleSnap = preferences_.scaleSnap;
  workspace_.sceneView().showBounds = preferences_.showBounds3D;
  workspace_.sceneView().showColliders = preferences_.showColliders3D;
  workspace_.sceneView().showLights = preferences_.showLights3D;
  workspace_.sceneView().showCameras = preferences_.showCameras3D;
  workspace_.sceneView2D().translationSnap = preferences_.translationSnap;
  workspace_.sceneView2D().rotationSnapDegrees =
      preferences_.rotationSnapDegrees;
  workspace_.sceneView2D().scaleSnap = preferences_.scaleSnap;
  workspace_.sceneView2D().showGrid = preferences_.showGrid2D;
  workspace_.sceneView2D().showBounds = preferences_.showBounds2D;
  workspace_.sceneView2D().showColliders = preferences_.showColliders2D;
  workspace_.sceneView2D().showCameras = preferences_.showCameras2D;
}

bool EditorShell::openDocument(const std::filesystem::path &path,
                               std::string &error) {
  if (isHudFile(path) && workspace_.authoredHudPath() &&
      std::filesystem::absolute(path).lexically_normal() ==
          std::filesystem::absolute(*workspace_.authoredHudPath())
              .lexically_normal()) {
    const EditorHudDocument *hud = workspace_.hudDocument();
    if (hud == nullptr || hud->preview().nodes.empty()) {
      error = "The current HUD could not be opened in the viewport.";
      return false;
    }
    workspace_.selectHudNode(hud->preview().nodes.front().id);
    return true;
  }
  return specializedPanel_.open(path, workspace_.assetIndex(), error);
}

void EditorShell::draw(const int width, const int height,
                       const std::string_view rendererName) {
  playSession_.poll();
  bool openClosePrompt = false;
  bool openRecoveryPrompt = false;
  if (exitRequested_) {
    exitRequested_ = false;
    if (workspace_.hasUnsavedChanges() || specializedPanel_.isDirty()) {
      openClosePrompt = true;
    } else {
      wantsExit_ = true;
    }
  }
  if (pendingRecovery_ && !recoveryPromptOpened_) {
    recoveryPromptOpened_ = true;
    openRecoveryPrompt = true;
  }
  ImGuiIO &input = ImGui::GetIO();
  if (!input.WantTextInput && input.KeyCtrl &&
      ImGui::IsKeyPressed(ImGuiKey_S, false)) {
    std::string error;
    if (workspace_.hudDirty() && !workspace_.saveHud(error))
      notice_ = error;
    else if (workspace_.sceneDocument().isDirty() && !workspace_.save(error))
      notice_ = error;
    else if (workspace_.projectDocument().isDirty() &&
             !workspace_.saveProject(error))
      notice_ = error;
    else
      notice_ = "Authored documents saved";
  }
  if (!input.WantTextInput && input.KeyCtrl &&
      ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
    std::string error;
    notice_ = workspace_.undo(error) ? "Undid scene edit" : error;
  }
  if (!input.WantTextInput && input.KeyCtrl &&
      ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
    std::string error;
    notice_ = workspace_.redo(error) ? "Redid scene edit" : error;
  }
  if (ImGui::IsKeyPressed(ImGuiKey_F5, false)) {
    std::string error;
    notice_ = workspace_.refresh(error) ? "Project refreshed" : error;
  }

  const float screenWidth = static_cast<float>(width);
  const float screenHeight = static_cast<float>(height);
  const EditorWorkspaceLayout layout =
      editorWorkspaceLayout(screenWidth, screenHeight);
  const float menuHeight = layout.menuHeight;
  const float toolbarHeight = layout.toolbarHeight;
  const float stageTabsHeight = layout.stageTabsHeight;
  const float statusHeight = layout.statusHeight;
  const float leftWidth = layout.leftWidth;
  const float rightWidth = layout.rightWidth;
  const float bottomHeight = layout.bottomHeight;
  const float contentTop = layout.contentTop;
  const float contentBottom = layout.contentBottom;
  const float upperHeight = layout.upperHeight;
  const float centerWidth = layout.centerWidth;
  const float consoleWidth = layout.consoleWidth;
  const float assetsWidth = layout.assetsWidth;

  const EditorProjectOperationSnapshot projectOperation =
      buildPanel_.operation();
  drawMenu(workspace_, {screenWidth, menuHeight}, exitRequested_, projectPanel_,
           animationMachinePanel_, buildPanel_, aboutPanel_, notice_);
  drawEditorToolbar({0.0F, menuHeight}, {screenWidth, toolbarHeight},
                    workspace_, playSession_, showGameView_, stepRequested_,
                    notice_);
  const runtime::World *runtimeWorld = playSession_.runtimeWorld();
  const bool runtimePanels = showGameView_ && runtimeWorld != nullptr;
  if (runtimePanels)
    drawRuntimeHierarchy(*runtimeWorld, {0.0F, contentTop},
                         {leftWidth, upperHeight}, selectedRuntimeEntityId_);
  else
    hierarchyPanel_.draw(workspace_, {0.0F, contentTop},
                         {leftWidth, upperHeight}, notice_);
  if (runtimePanels)
    playSession_.setDebugFocus(selectedRuntimeEntityId_);
  drawStageTabs({leftWidth, contentTop}, {centerWidth, stageTabsHeight},
                showGameView_);
  const ImVec2 stagePosition{leftWidth, contentTop + stageTabsHeight};
  const ImVec2 stageSize{centerWidth, upperHeight - stageTabsHeight};
  if (showGameView_) {
    viewportArea_ = {};
    drawEditorGameView(playSession_, stagePosition, stageSize,
                       gameTextureIndex_, gameArea_, gameViewFocused_);
  } else {
    gameArea_ = {};
    gameViewFocused_ = false;
    drawEditorViewport(workspace_, stagePosition, stageSize, viewportArea_,
                       hudViewportState_, notice_);
  }
  if (runtimePanels)
    drawRuntimeInspector(*runtimeWorld, {screenWidth - rightWidth, contentTop},
                         {rightWidth, contentBottom - contentTop},
                         selectedRuntimeEntityId_);
  else if (!workspace_.selectedHudNodeId().empty())
    drawEditorHudNodeInspector(
        workspace_, {screenWidth - rightWidth, contentTop},
        {rightWidth, contentBottom - contentTop}, hudInspectorState_, notice_);
  else
    drawInspectorPanel(workspace_, {screenWidth - rightWidth, contentTop},
                       {rightWidth, contentBottom - contentTop},
                       inspectorState_, notice_);
  consolePanel_.draw(workspace_, playSession_, {0.0F, contentTop + upperHeight},
                     {consoleWidth, bottomHeight}, projectOperation, notice_);
  if (auto source = consolePanel_.takeOpenRequest()) {
    std::string error;
    if (!openDocument(*source, error))
      notice_ = "Diagnostic source: " + source->string();
  }
  assetsPanel_.draw(workspace_, {consoleWidth, contentTop + upperHeight},
                    {assetsWidth, bottomHeight}, notice_);
  if (auto source = assetsPanel_.takeOpenRequest()) {
    std::string error;
    if (!openDocument(*source, error))
      notice_ = error;
  }
  buildPanel_.draw(workspace_, notice_);
  drawStatus(workspace_, {0.0F, contentBottom}, {screenWidth, statusHeight},
             rendererName, notice_, buildPanel_.linuxTarget(),
             buildPanel_.androidTarget());
  conflictPanel_.draw(workspace_, notice_);
  projectPanel_.draw(workspace_, notice_);
  specializedPanel_.draw(workspace_, notice_);
  animationMachinePanel_.draw(workspace_, notice_);
  aboutPanel_.draw(brandingTextureIndex_, notice_);

  ImGui::SetNextWindowPos({0.0F, 0.0F}, ImGuiCond_Always);
  ImGui::SetNextWindowSize({1.0F, 1.0F}, ImGuiCond_Always);
  ImGui::Begin("##editor-modal-host", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoInputs |
                   ImGuiWindowFlags_NoBringToFrontOnFocus);
  if (openRecoveryPrompt)
    ImGui::OpenPopup("Recover editor session");
  if (ImGui::BeginPopupModal("Recover editor session", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted(
        "Unsaved documents from an interrupted editor session were found.");
    ImGui::TextDisabled("Restoring loads them as unsaved memory state only.");
    for (const EditorRecoveryDocument &document : pendingRecovery_->documents)
      ImGui::BulletText("%s · %s", document.kind.c_str(),
                        document.path.filename().string().c_str());
    if (ImGui::Button("Restore into editor")) {
      std::string error;
      EditorRecoverySnapshot workspaceRecovery = *pendingRecovery_;
      std::erase_if(workspaceRecovery.documents, [](const auto &document) {
        return document.kind == "specialized";
      });
      bool restored = workspace_.applyRecovery(workspaceRecovery, error);
      if (restored)
        for (const EditorRecoveryDocument &document :
             pendingRecovery_->documents)
          if (document.kind == "specialized" &&
              !specializedPanel_.restore(document, workspace_, error)) {
            restored = false;
            break;
          }
      if (restored) {
        notice_ = "Recovered documents loaded as unsaved changes";
        pendingRecovery_.reset();
        recoveryPromptOpened_ = false;
        ImGui::CloseCurrentPopup();
      } else {
        notice_ = error;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard recovery")) {
      std::string error;
      if (!recoveryStore_.discard(workspace_.projectPath(), error))
        notice_ = error;
      else {
        pendingRecovery_.reset();
        recoveryPromptOpened_ = false;
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::EndPopup();
  }

  if (openClosePrompt)
    ImGui::OpenPopup("Unsaved changes");
  if (ImGui::BeginPopupModal("Unsaved changes", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Save changes before closing the editor?");
    for (const EditorRecoveryDocument &document : workspace_.dirtyDocuments())
      ImGui::BulletText("%s · %s", document.kind.c_str(),
                        document.path.filename().string().c_str());
    if (const auto specialized = specializedPanel_.recoveryDocument())
      ImGui::BulletText("%s · %s", specialized->kind.c_str(),
                        specialized->path.filename().string().c_str());
    if (ImGui::Button("Save all and exit")) {
      std::string error;
      if (workspace_.saveAll(error) &&
          specializedPanel_.saveActive(workspace_, error)) {
        (void)recoveryStore_.discard(workspace_.projectPath(), error);
        wantsExit_ = true;
        ImGui::CloseCurrentPopup();
      } else {
        notice_ = error;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard and exit")) {
      std::string error;
      (void)recoveryStore_.discard(workspace_.projectPath(), error);
      specializedPanel_.discardActive();
      wantsExit_ = true;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  ImGui::End();

  if (!pendingRecovery_ && !recoverySyncBlocked_ && !wantsExit_) {
    std::vector<EditorRecoveryDocument> recovery = workspace_.dirtyDocuments();
    if (auto specialized = specializedPanel_.recoveryDocument())
      recovery.push_back(std::move(*specialized));
    nlohmann::json fingerprint = nlohmann::json::array();
    for (const auto &document : recovery)
      fingerprint.push_back({document.path.string(), document.content});
    const std::string canonical = fingerprint.dump();
    if (canonical != recoveryFingerprint_) {
      std::string error;
      if (!recoveryStore_.update(workspace_.projectPath(), recovery, error))
        notice_ = "Recovery cache: " + error;
      else
        recoveryFingerprint_ = canonical;
    }
  }
  const EditorPreferences currentPreferences{
      .translationSnap =
          workspace_.viewDimension() == EditorSceneViewDimension::TwoDimensional
              ? workspace_.sceneView2D().translationSnap
              : workspace_.sceneView().translationSnap,
      .rotationSnapDegrees =
          workspace_.viewDimension() == EditorSceneViewDimension::TwoDimensional
              ? workspace_.sceneView2D().rotationSnapDegrees
              : workspace_.sceneView().rotationSnapDegrees,
      .scaleSnap =
          workspace_.viewDimension() == EditorSceneViewDimension::TwoDimensional
              ? workspace_.sceneView2D().scaleSnap
              : workspace_.sceneView().scaleSnap,
      .showBounds3D = workspace_.sceneView().showBounds,
      .showColliders3D = workspace_.sceneView().showColliders,
      .showLights3D = workspace_.sceneView().showLights,
      .showCameras3D = workspace_.sceneView().showCameras,
      .showGrid2D = workspace_.sceneView2D().showGrid,
      .showBounds2D = workspace_.sceneView2D().showBounds,
      .showColliders2D = workspace_.sceneView2D().showColliders,
      .showCameras2D = workspace_.sceneView2D().showCameras};
  if (!preferenceSyncBlocked_ && preferences_ != currentPreferences) {
    preferences_ = currentPreferences;
    std::string error;
    if (!preferencesStore_.save(preferences_, error))
      notice_ = "Editor preferences: " + error;
  }
}

} // namespace demi::editor
