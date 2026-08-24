#include "editor/EditorShell.h"

#include "editor/EditorChrome.h"
#include "editor/EditorHudNodeInspector.h"
#include "editor/EditorInspectorPanel.h"
#include "editor/EditorPanelStyle.h"
#include "editor/EditorToolbar.h"
#include "editor/EditorViewportPanel.h"

#include "demi/core/Version.h"
#include "demi/filesystem/ProjectPaths.h"
#include "demi/schema/Validation.h"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace demi::editor {
namespace {

void drawConsole(EditorWorkspace &workspace, const ImVec2 position,
                 const ImVec2 size,
                 const EditorProjectOperationSnapshot &operation) {
  beginEditorPanel("Console", position, size);
  if (ImGui::BeginTabBar("diagnostic-tabs")) {
    if (ImGui::BeginTabItem("Console")) {
      if (workspace.diagnostics().empty())
        ImGui::TextColored({0.35F, 0.85F, 0.55F, 1.0F},
                           "Project validation is clean.");
      for (const Diagnostic &diagnostic : workspace.diagnostics()) {
        const ImVec4 color = diagnostic.severity == Severity::Error
                                 ? ImVec4{0.95F, 0.34F, 0.38F, 1.0F}
                                 : ImVec4{0.95F, 0.72F, 0.30F, 1.0F};
        ImGui::TextColored(color, "%s", diagnostic.code.c_str());
        ImGui::SameLine();
        ImGui::TextWrapped("%s", diagnostic.message.c_str());
      }
      if (operation.result)
        for (const Diagnostic &diagnostic : operation.result->diagnostics) {
          const ImVec4 color = diagnostic.severity == Severity::Error
                                   ? ImVec4{0.95F, 0.34F, 0.38F, 1.0F}
                                   : ImVec4{0.95F, 0.72F, 0.30F, 1.0F};
          ImGui::TextColored(color, "%s", diagnostic.code.c_str());
          ImGui::SameLine();
          ImGui::TextWrapped("%s", diagnostic.message.c_str());
        }
      ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem("Profiler")) {
      ImGui::TextDisabled(
          "Profiler data appears when a play session is attached.");
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

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

void drawMenu(EditorWorkspace &workspace, const ImVec2 size, bool &wantsExit,
              EditorProjectPanel &projectPanel,
              EditorAnimationMachinePanel &animationPanel,
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
      if (ImGui::MenuItem("Exit")) {
        if (workspace.hasUnsavedChanges())
          notice = "Save or undo scene and project changes before exiting.";
        else
          wantsExit = true;
      }
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
    ImGui::MenuItem("Build");
    ImGui::MenuItem("Help");
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

EditorShell::EditorShell(EditorWorkspace &workspace) : workspace_(workspace) {}

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
  constexpr float menuHeight = 32.0F;
  constexpr float toolbarHeight = 52.0F;
  constexpr float stageTabsHeight = 31.0F;
  constexpr float statusHeight = 27.0F;
  const float leftWidth = std::clamp(screenWidth * 0.195F, 240.0F, 340.0F);
  const float rightWidth = std::clamp(screenWidth * 0.225F, 285.0F, 405.0F);
  const float bottomHeight = std::clamp(screenHeight * 0.30F, 205.0F, 292.0F);
  const float contentTop = menuHeight + toolbarHeight;
  const float contentBottom = screenHeight - statusHeight;
  const float upperHeight =
      std::max(180.0F, contentBottom - contentTop - bottomHeight);
  const float centerWidth =
      std::max(320.0F, screenWidth - leftWidth - rightWidth);
  const float consoleWidth = std::clamp(screenWidth * 0.26F, 310.0F, 470.0F);
  const float buildWidth = std::clamp(screenWidth * 0.15F, 220.0F, 280.0F);
  const float assetsWidth =
      std::max(280.0F, screenWidth - consoleWidth - rightWidth - buildWidth);

  const EditorProjectOperationSnapshot projectOperation =
      buildPanel_.operation();
  drawMenu(workspace_, {screenWidth, menuHeight}, wantsExit_, projectPanel_,
           animationMachinePanel_, notice_);
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
                       {rightWidth, contentBottom - contentTop}, notice_);
  drawConsole(workspace_, {0.0F, contentTop + upperHeight},
              {consoleWidth, bottomHeight}, projectOperation);
  assetsPanel_.draw(workspace_, {consoleWidth, contentTop + upperHeight},
                    {assetsWidth, bottomHeight}, notice_);
  if (auto source = assetsPanel_.takeOpenRequest()) {
    std::string error;
    if (!openDocument(*source, error))
      notice_ = error;
  }
  buildPanel_.draw(workspace_,
                   {consoleWidth + assetsWidth, contentTop + upperHeight},
                   {buildWidth, bottomHeight}, notice_);
  drawStatus(workspace_, {0.0F, contentBottom}, {screenWidth, statusHeight},
             rendererName, notice_, buildPanel_.linuxTarget(),
             buildPanel_.androidTarget());
  conflictPanel_.draw(workspace_, notice_);
  projectPanel_.draw(workspace_, notice_);
  specializedPanel_.draw(workspace_, notice_);
  animationMachinePanel_.draw(workspace_, notice_);
}

} // namespace demi::editor
