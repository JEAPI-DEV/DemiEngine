#include "editor/EditorShell.h"
#include "demi/runtime/platform/ExternalProcess.h"
#include "editor/EditorCodeEditor.h"
#include "editor/EditorSettingsPanel.h"

#include "editor/EditorChrome.h"
#include "editor/EditorDefaultLayout.h"
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
#include <functional>
#include <string>
#include <string_view>

namespace demi::editor {
namespace {

void drawMenu(EditorWorkspace &workspace, EditorDocumentSessions &documents,
              const ImVec2 size, bool &exitRequested,
              EditorProjectPanel &projectPanel, EditorAssetsPanel &assetsPanel,
              EditorAnimationMachinePanel &animationPanel,
              EditorBuildPanel &buildPanel, EditorAboutPanel &aboutPanel,
              EditorDockingWorkspace &dockingWorkspace, bool &showSettings,
              std::string &notice, const EditorKeyBindings &bindings,
              EditorCommandContext context, bool uiPaletteAvailable,
              bool terrainNodesAvailable, bool allowCameraEdit,
              const std::function<void(EditorCommand)> &execute) {
  beginEditorShellPanel("MainMenu", {0.0F, 0.0F}, size,
                        ImGuiWindowFlags_MenuBar |
                            ImGuiWindowFlags_NoScrollbar);
  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("New project..."))
        projectPanel.openCreateProject();
      if (ImGui::BeginMenu("Create")) {
        const auto sourceItem = [&](const char *label, EditorSourceKind kind) {
          if (ImGui::MenuItem(label)) {
            dockingWorkspace.visibility().assets = true;
            assetsPanel.openCreate(kind);
          }
        };
        if (ImGui::BeginMenu("Scene")) {
          sourceItem("2D Scene", EditorSourceKind::Scene2D);
          sourceItem("3D Scene", EditorSourceKind::Scene3D);
          ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Prefab")) {
          sourceItem("2D Entity Prefab", EditorSourceKind::Prefab2D);
          sourceItem("3D Entity Prefab", EditorSourceKind::Prefab);
          sourceItem("UI Prefab", EditorSourceKind::UiPrefab);
          ImGui::EndMenu();
        }
        sourceItem("HUD", EditorSourceKind::Hud);
        if (ImGui::BeginMenu("Terrain")) {
          sourceItem("Terrain", EditorSourceKind::Terrain);
          sourceItem("Material", EditorSourceKind::TerrainMaterial);
          sourceItem("Material Set", EditorSourceKind::TerrainMaterialSet);
          sourceItem("Palette", EditorSourceKind::TerrainPalette);
          ImGui::EndMenu();
        }
        sourceItem("Lua Script", EditorSourceKind::Lua);
        ImGui::EndMenu();
      }
      if (ImGui::MenuItem("Project settings..."))
        projectPanel.openSettings();
      ImGui::Separator();
      const auto saveShortcut = bindings.label(EditorCommand::SaveAll);
      if (ImGui::MenuItem("Save all", saveShortcut.c_str()))
        execute(EditorCommand::SaveAll);
      if (ImGui::MenuItem("Save active document", nullptr, false,
                          workspace.activeDocumentDirty())) {
        std::string error;
        bool saved = workspace.save(error);
        if (saved &&
            workspace.activeDocument() == EditorWorkspaceDocument::Hud &&
            workspace.hudDocument())
          saved = documents.refreshHudReferences(
              workspace.hudDocument()->path(), error);
        if (saved && workspace.isPrefabDocument() &&
            workspace.activeDocument() == EditorWorkspaceDocument::Scene)
          saved = documents.refreshPrefabReferences(error);
        notice = saved ? "Document saved" : error;
      }
      if (ImGui::MenuItem("Save project", nullptr, false,
                          documents.scene().projectDocument().isDirty())) {
        std::string error;
        notice = documents.scene().saveProject(error) &&
                         documents.refreshProjectReferences(error)
                     ? "Project saved"
                     : error;
      }
      const auto refreshShortcut = bindings.label(EditorCommand::Refresh);
      if (ImGui::MenuItem("Refresh project", refreshShortcut.c_str()))
        execute(EditorCommand::Refresh);
      ImGui::Separator();
      if (ImGui::MenuItem("Exit"))
        exitRequested = true;
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Editor Settings..."))
        showSettings = true;
      ImGui::Separator();
      const bool graph = context == EditorCommandContext::TerrainGraph;
      for (const auto command :
           {EditorCommand::Undo, EditorCommand::Redo, EditorCommand::Copy,
            EditorCommand::Cut, EditorCommand::Paste, EditorCommand::Duplicate,
            EditorCommand::Delete, EditorCommand::SelectAll}) {
        const auto &definition = editorCommandDefinition(command);
        const auto shortcut = bindings.label(command);
        bool enabled = editorCommandAvailable(command, context);
        if (!graph && command == EditorCommand::Undo)
          enabled = enabled && workspace.activeDocumentCanUndo();
        if (!graph && command == EditorCommand::Redo)
          enabled = enabled && workspace.activeDocumentCanRedo();
        if (ImGui::MenuItem(definition.label.data(), shortcut.c_str(), false,
                            enabled))
          execute(command);
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      dockingWorkspace.drawViewMenu(uiPaletteAvailable, terrainNodesAvailable);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Scene", workspace.activeDocument() ==
                                      EditorWorkspaceDocument::Scene)) {
      if (ImGui::BeginMenu("Add prefab instance")) {
        bool found = false;
        for (const auto &source : workspace.sources()) {
          if (!isPrefabFile(source))
            continue;
          found = true;
          const std::string label =
              source
                  .lexically_relative(
                      workspace.project().project.projectDirectory / "prefabs")
                  .generic_string();
          if (ImGui::MenuItem(label.c_str())) {
            std::string error;
            notice = workspace.instantiatePrefab(source, error)
                         ? "Prefab instance added"
                         : error;
          }
        }
        if (!found)
          ImGui::TextDisabled("Create an entity prefab first.");
        ImGui::EndMenu();
      }
      const auto selected = workspace.selectedEntityId();
      if (ImGui::MenuItem("Create prefab from selection...", nullptr, false,
                          !selected.empty() &&
                              workspace.sceneDocument().entity(selected))) {
        dockingWorkspace.visibility().assets = true;
        assetsPanel.openCreate(EditorSourceKind::PrefabFromSelection,
                               std::string(selected));
      }
      if (ImGui::MenuItem("Align selected camera to view", nullptr, false,
                          allowCameraEdit &&
                              workspace.canAlignSelectedCameraToView())) {
        std::string error;
        notice = workspace.alignSelectedCameraToView(error)
                     ? "Camera aligned to view"
                     : error;
      }
      if (ImGui::BeginMenu("HUD", !workspace.isPrefabDocument())) {
        std::string error;
        if (ImGui::MenuItem("None"))
          notice = workspace.setSceneHud({}, error) ? "HUD detached" : error;
        for (const auto &source : workspace.sources())
          if (isHudFile(source) &&
              ImGui::MenuItem(source.filename().string().c_str())) {
            notice = workspace.setSceneHud(source, error) ? "Scene HUD assigned"
                                                          : error;
            break;
          }
        ImGui::EndMenu();
      }
      ImGui::EndMenu();
    }
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
  beginEditorShellPanel("Status", position, size,
                        ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoInputs);
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
  if (workspace.activeDocumentDirty())
    ImGui::TextColored({0.95F, 0.67F, 0.28F, 1.0F}, "Modified");
  else
    ImGui::TextColored({0.32F, 0.86F, 0.49F, 1.0F}, "Ready");
  ImGui::End();
}

} // namespace

EditorShell::EditorShell(EditorWorkspace &sceneWorkspace)
    : documents_(sceneWorkspace),
      dockingWorkspace_(defaultEditorDataDirectory()),
      recoveryStore_(defaultEditorCacheDirectory()),
      preferencesStore_(defaultEditorDataDirectory()) {
  std::string error;
  pendingRecovery_ = recoveryStore_.load(workspace().projectPath(), error);
  if (!error.empty()) {
    notice_ = "Recovery cache: " + error;
    recoverySyncBlocked_ = true;
  }
  error.clear();
  if (!preferencesStore_.load(preferences_, error)) {
    notice_ = "Editor preferences: " + error;
    preferenceSyncBlocked_ = true;
  }
  focusWindow_ = "Viewport";
  uiScale_ = preferences_.uiScale;
  persistedPreferences_ = preferences_;
  applyViewportPreferences(documents_.scene());
  if (const auto *hud = documents_.scene().hudDocument()) {
    const auto path = hud->path();
    if (!documents_.openHud(path, error))
      notice_ = error;
    (void)documents_.focus(EditorDocumentSession::Scene, error);
  }
  if (std::string diagnostic = dockingWorkspace_.takeDiagnostic();
      !diagnostic.empty())
    notice_ = std::move(diagnostic);
}

bool EditorShell::openDocument(const std::filesystem::path &path,
                               std::string &error) {
  if (path.extension() == ".lua") {
    const auto project = workspace().project().project.projectDirectory;
    if (!prepareCodeEditorWorkspace(
            project, std::filesystem::path(DEMI_SOURCE_DIR) / "scripts/stubs",
            error))
      return false;
    return runtime::platform::launchExternalProcess(
        codeEditorCommand(preferences_, project, path), error);
  }
  auto &panels = dockingWorkspace_.visibility();
  if (isPrefabFile(path)) {
    auto *previous = documents_.workspace(EditorDocumentSession::Prefab);
    if (!documents_.openPrefab(path, error))
      return false;
    if (graphWorkspace_ == previous &&
        previous != documents_.workspace(EditorDocumentSession::Prefab)) {
      graphWorkspace_ = nullptr;
      terrainGraphPanel_.close();
    }
    applyViewportPreferences(workspace());
    panels.prefab = true;
    focusWindow_ = "Prefab";
    showHudView_ = showGameView_ = showTerrainGraphView_ = false;
    return true;
  }
  if (isSceneFile(path)) {
    const auto normalized = std::filesystem::absolute(path).lexically_normal();
    const auto &project = documents_.scene().project().project;
    const bool registered =
        std::ranges::any_of(project.scenes, [&](const auto &entry) {
          return std::filesystem::absolute(project.projectDirectory /
                                           entry.path)
                     .lexically_normal() == normalized;
        });
    if (!registered && !documents_.refreshProjectReferences(error))
      return false;
    if (!documents_.scene().openSceneDocument(path, error))
      return false;
    if (graphWorkspace_ == &documents_.scene()) {
      graphWorkspace_ = nullptr;
      terrainGraphPanel_.close();
    }
    focusAuthoring(EditorDocumentSession::Scene, false);
    panels.viewport = true;
    focusWindow_ = "Viewport";
    return true;
  }
  if (isHudFile(path) || isUiPrefabFile(path)) {
    if (!documents_.openHud(path, error))
      return false;
    focusAuthoring(EditorDocumentSession::Hud, true);
    panels.hud = true;
    focusWindow_ = "HUD";
    return true;
  }
  const auto *terrainRecord = workspace().assetIndex().findBySource(path);
  if (!terrainRecord)
    terrainRecord = workspace().assetIndex().findByManifest(path);
  if (terrainRecord && terrainRecord->manifest.type == "Terrain") {
    auto *previous = documents_.workspace(EditorDocumentSession::TerrainAsset);
    if (!documents_.openTerrainAsset(path, error))
      return false;
    if (graphWorkspace_ == previous &&
        previous != documents_.workspace(EditorDocumentSession::TerrainAsset)) {
      graphWorkspace_ = nullptr;
      terrainGraphPanel_.close();
    }
    applyViewportPreferences(workspace());
    panels.terrainAsset = true;
    focusWindow_ = "Terrain Asset";
    showHudView_ = showGameView_ = showTerrainGraphView_ = false;
    return true;
  }
  return specializedPanel_.open(path, workspace().assetIndex(), error);
}

bool EditorShell::openTerrainGraph(std::string &error) {
  workspace().syncTerrainAuthoring();
  if (!workspace().pinTerrainAuthoring(workspace().selectedEntityId(), error))
    return false;
  graphWorkspace_ = &workspace();
  terrainGraphPanel_.open(workspace());
  showTerrainGraphView_ = true;
  showHudView_ = showGameView_ = false;
  dockingWorkspace_.visibility().terrainGraph = true;
  focusWindow_ = "Terrain Graph";
  return true;
}

bool EditorShell::openTerrainNodeSettings(std::string_view nodeId,
                                          std::string &error) {
  if (!openTerrainGraph(error))
    return false;
  return terrainGraphPanel_.openNodeSettings(workspace(), nodeId, error);
}

void EditorShell::draw(const int width, const int height,
                       const std::string_view rendererName) {
  workspace().pollScriptSources();
  std::string terrainError;
  if (!documents_.pollTerrain(terrainError))
    notice_ = std::move(terrainError);
  if (!workspace().hasHudDocument())
    showHudView_ = false;
  bool openClosePrompt = false;
  bool openRecoveryPrompt = false;
  if (exitRequested_) {
    exitRequested_ = false;
    if (documents_.hasUnsavedChanges() || specializedPanel_.isDirty()) {
      openClosePrompt = true;
    } else {
      wantsExit_ = true;
    }
  }
  if (pendingRecovery_ && !recoveryPromptOpened_) {
    recoveryPromptOpened_ = true;
    openRecoveryPrompt = true;
  }

  const float screenWidth = static_cast<float>(width);
  const float screenHeight = static_cast<float>(height);
  const EditorWorkspaceLayout layout =
      editorWorkspaceLayout(screenWidth, screenHeight);
  const float menuHeight = layout.menuHeight;
  const float toolbarHeight = layout.toolbarHeight;
  const float statusHeight = layout.statusHeight;
  const float contentTop = layout.contentTop;
  const float contentBottom = layout.contentBottom;
  const float leftWidth = EditorDefaultLayout::HierarchyWidth;
  const float rightWidth = EditorDefaultLayout::InspectorWidth;
  const float bottomHeight = EditorDefaultLayout::BottomHeight;
  const float upperHeight =
      std::max(layout.dockspaceHeight - bottomHeight, 1.0F);
  const float centerWidth =
      std::max(screenWidth - leftWidth - rightWidth, 1.0F);
  const float consoleWidth = EditorDefaultLayout::ConsoleWidth;
  const float assetsWidth =
      std::max(screenWidth - rightWidth - consoleWidth, 1.0F);

  const EditorProjectOperationSnapshot projectOperation =
      buildPanel_.operation();
  const auto focusedContext = commandContext();
  if (focusedContext != EditorCommandContext::None &&
      focusedContext != EditorCommandContext::Game)
    lastAuthoringContext_ = focusedContext;
  if (lastAuthoringContext_ == EditorCommandContext::TerrainGraph &&
      !showTerrainGraphView_)
    lastAuthoringContext_ =
        workspace().activeDocument() == EditorWorkspaceDocument::Hud
            ? EditorCommandContext::Hud
            : EditorCommandContext::Scene;
  const auto menuContext =
      showGameView_ ? EditorCommandContext::Game : lastAuthoringContext_;
  drawMenu(workspace(), documents_, {screenWidth, menuHeight}, exitRequested_,
           projectPanel_, assetsPanel_, animationMachinePanel_, buildPanel_,
           aboutPanel_, dockingWorkspace_, showSettings_, notice_,
           preferences_.keyBindings, menuContext,
           showHudView_ && !showGameView_,
           showTerrainGraphView_ && !showGameView_,
           !showGameView_ && !playSession_.isRunning() &&
               playSession_.state() != EditorPlayState::Starting,
           [this, menuContext](EditorCommand command) {
             executeCommand(command, menuContext);
           });
  drawEditorSettingsPanel(showSettings_, uiScale_, preferences_,
                          shortcutSettings_);
  const auto previousPlayState = playSession_.state();
  drawEditorToolbar({0.0F, menuHeight}, {screenWidth, toolbarHeight},
                    workspace(), documents_.scene(), playSession_, runPanel_,
                    showGameView_, stepRequested_, notice_,
                    preferences_.keyBindings, menuContext,
                    [this, menuContext](EditorCommand command) {
                      executeCommand(command, menuContext);
                    });
  if (previousPlayState != playSession_.state() && playSession_.isEmbedded())
    focusWindow_ = "Game View";
  auto &panels = dockingWorkspace_.visibility();
  if (previousPlayState != playSession_.state() && playSession_.isEmbedded())
    panels.game = true;
  dockingWorkspace_.drawDockspace({0.0F, contentTop},
                                  {screenWidth, contentBottom - contentTop});
  drawDocumentViews({leftWidth, contentTop}, {centerWidth, upperHeight});
  const runtime::World *runtimeWorld = playSession_.runtimeWorld();
  const bool runtimePanels = showGameView_ && runtimeWorld != nullptr;
  if (panels.hierarchy && runtimePanels)
    drawRuntimeHierarchy(*runtimeWorld, {0.0F, contentTop},
                         {leftWidth, upperHeight}, selectedRuntimeEntityId_,
                         &panels.hierarchy);
  else if (panels.hierarchy)
    hierarchyPanel_.draw(workspace(), {0.0F, contentTop},
                         {leftWidth, upperHeight}, showHudView_, notice_,
                         &panels.hierarchy);
  if (!runtimePanels &&
      workspace().activeDocument() == EditorWorkspaceDocument::Hud &&
      &workspace() != documents_.workspace(EditorDocumentSession::Hud)) {
    const auto *hud = workspace().hudDocument();
    const auto selected = std::string(workspace().selectedHudNodeId());
    if (hud) {
      const auto path = hud->path();
      std::string error;
      if (documents_.openHud(path, error)) {
        workspace().selectHudNode(selected);
        showHudView_ = true;
        panels.hud = true;
        focusWindow_ = "HUD";
      } else
        notice_ = error;
    }
  }
  if (runtimePanels)
    playSession_.setDebugFocus(selectedRuntimeEntityId_);
  if (panels.inspector && runtimePanels)
    drawRuntimeInspector(*runtimeWorld, {screenWidth - rightWidth, contentTop},
                         {rightWidth, contentBottom - contentTop},
                         selectedRuntimeEntityId_, &panels.inspector);
  else if (panels.inspector)
    drawEditorInspector(workspace(), {screenWidth - rightWidth, contentTop},
                        {rightWidth, contentBottom - contentTop},
                        inspectorState_, hudInspectorState_, notice_,
                        &panels.inspector);
  if (!showGameView_ && showHudView_ && panels.uiPalette)
    drawEditorPalettePanel(workspace(), modulesState_,
                           EditorModuleKind::HudElement, &panels.uiPalette);
  if (!showGameView_ && showTerrainGraphView_ && graphWorkspace_ &&
      panels.terrainNodes)
    drawEditorPalettePanel(*graphWorkspace_, modulesState_,
                           EditorModuleKind::TerrainNode, &panels.terrainNodes);
  if (workspace().takeTerrainGraphOpenRequest()) {
    std::string error;
    if (!openTerrainGraph(error))
      notice_ = error;
  }
  if (auto selected = workspace().takeTerrainAssetCreateRequest()) {
    panels.assets = true;
    assetsPanel_.openCreate(selected->empty()
                                ? EditorSourceKind::Terrain
                                : EditorSourceKind::TerrainFromSelection,
                            *selected);
  }
  if (auto assignment = workspace().takeTerrainAssetAssignRequest()) {
    std::string error;
    notice_ = workspace().assignTerrainAsset(assignment->first,
                                             assignment->second, error)
                  ? "Terrain asset assigned"
                  : error;
  }
  if (auto source = workspace().takeTerrainAssetOpenRequest()) {
    std::string error;
    if (!openDocument(*source, error))
      documentOpenError_ = error;
  }
  consolePanel_.draw(
      workspace(), playSession_, {0.0F, contentTop + upperHeight},
      {consoleWidth, bottomHeight}, projectOperation, notice_, panels);
  if (auto source = consolePanel_.takeOpenRequest()) {
    std::string error;
    if (!openDocument(*source, error))
      notice_ = "Diagnostic source: " + source->string();
  }
  if (inspectorState_.openRequest) {
    const auto source =
        std::exchange(inspectorState_.openRequest, std::nullopt);
    std::string error;
    if (!openDocument(*source, error))
      documentOpenError_ = error;
  }
  if (!showGameView_ && showTerrainGraphView_ && graphWorkspace_ &&
      panels.terrainPresets &&
      terrainPresetsPanel_.draw(*graphWorkspace_, notice_,
                                &panels.terrainPresets))
    terrainGraphPanel_.resetDraftHistory();
  if (panels.assets)
    assetsPanel_.draw(workspace(), {consoleWidth, contentTop + upperHeight},
                      {assetsWidth, bottomHeight}, notice_, &panels.assets);
  if (auto source = assetsPanel_.takeOpenRequest()) {
    std::string error;
    if (!openDocument(*source, error))
      documentOpenError_ = error;
  }
  if (!documentOpenError_.empty())
    ImGui::OpenPopup("Cannot open document");
  if (ImGui::BeginPopupModal("Cannot open document", nullptr,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("%s", documentOpenError_.c_str());
    if (ImGui::Button("OK")) {
      documentOpenError_.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  buildPanel_.draw(workspace(), notice_);
  runPanel_.draw(documents_.scene(), playSession_.isRunning(),
                 [this](std::string &error) { return documents_.saveAll(error); }, notice_);
  drawStatus(workspace(), {0.0F, contentBottom}, {screenWidth, statusHeight},
             rendererName, notice_, buildPanel_.linuxTarget(),
             buildPanel_.androidTarget());
  conflictPanel_.draw(workspace(), notice_);
  projectPanel_.draw(documents_.scene(), notice_);
  specializedPanel_.draw(workspace(), notice_);
  animationMachinePanel_.draw(workspace(), notice_);
  aboutPanel_.draw(brandingTextureIndex_, notice_);
  dockingWorkspace_.persistVisibilityIfChanged();
  if (std::string diagnostic = dockingWorkspace_.takeDiagnostic();
      !diagnostic.empty())
    notice_ = std::move(diagnostic);

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
      bool restored = documents_.applyRecovery(workspaceRecovery, error);
      if (restored)
        for (const EditorRecoveryDocument &document :
             pendingRecovery_->documents)
          if (document.kind == "specialized" &&
              !specializedPanel_.restore(document, workspace(), error)) {
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
      if (!recoveryStore_.discard(workspace().projectPath(), error))
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
    for (const EditorRecoveryDocument &document : documents_.dirtyDocuments())
      ImGui::BulletText("%s · %s", document.kind.c_str(),
                        document.path.filename().string().c_str());
    if (const auto specialized = specializedPanel_.recoveryDocument())
      ImGui::BulletText("%s · %s", specialized->kind.c_str(),
                        specialized->path.filename().string().c_str());
    if (ImGui::Button("Save all and exit")) {
      std::string error;
      if (documents_.saveAll(error) &&
          specializedPanel_.saveActive(workspace(), error)) {
        (void)recoveryStore_.discard(workspace().projectPath(), error);
        wantsExit_ = true;
        ImGui::CloseCurrentPopup();
      } else {
        notice_ = error;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard and exit")) {
      std::string error;
      (void)recoveryStore_.discard(workspace().projectPath(), error);
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
    std::vector<EditorRecoveryDocument> recovery = documents_.dirtyDocuments();
    if (auto specialized = specializedPanel_.recoveryDocument())
      recovery.push_back(std::move(*specialized));
    nlohmann::json fingerprint = nlohmann::json::array();
    for (const auto &document : recovery)
      fingerprint.push_back({document.path.string(), document.content});
    const std::string canonical = fingerprint.dump();
    if (canonical != recoveryFingerprint_) {
      std::string error;
      if (!recoveryStore_.update(workspace().projectPath(), recovery, error))
        notice_ = "Recovery cache: " + error;
      else
        recoveryFingerprint_ = canonical;
    }
  }
  if (!focusWindow_.empty() && dockingWorkspace_.focusPanel(focusWindow_))
    focusWindow_.clear();
  dispatchShortcuts();
  const EditorPreferences currentPreferences{
      .uiScale = uiScale_,
      .translationSnap = workspace().viewDimension() ==
                                 EditorSceneViewDimension::TwoDimensional
                             ? workspace().sceneView2D().translationSnap
                             : workspace().sceneView().translationSnap,
      .rotationSnapDegrees = workspace().viewDimension() ==
                                     EditorSceneViewDimension::TwoDimensional
                                 ? workspace().sceneView2D().rotationSnapDegrees
                                 : workspace().sceneView().rotationSnapDegrees,
      .scaleSnap = workspace().viewDimension() ==
                           EditorSceneViewDimension::TwoDimensional
                       ? workspace().sceneView2D().scaleSnap
                       : workspace().sceneView().scaleSnap,
      .showBounds3D = workspace().sceneView().showBounds,
      .showColliders3D = workspace().sceneView().showColliders,
      .showLights3D = workspace().sceneView().showLights,
      .showCameras3D = workspace().sceneView().showCameras,
      .showGrid2D = workspace().sceneView2D().showGrid,
      .showBounds2D = workspace().sceneView2D().showBounds,
      .showColliders2D = workspace().sceneView2D().showColliders,
      .showCameras2D = workspace().sceneView2D().showCameras,
      .codeEditor = preferences_.codeEditor,
      .codeEditorArguments = preferences_.codeEditorArguments,
      .keyBindings = preferences_.keyBindings};
  if (!preferenceSyncBlocked_ && persistedPreferences_ != currentPreferences) {
    preferences_ = currentPreferences;
    std::string error;
    if (!preferencesStore_.save(preferences_, error))
      notice_ = "Editor preferences: " + error;
    else
      persistedPreferences_ = preferences_;
  }
}

} // namespace demi::editor
