#include "editor/EditorShell.h"

#include "editor/EditorInspectorPanel.h"
#include "editor/EditorPanelStyle.h"

#include "demi/core/Version.h"
#include "demi/filesystem/ProjectPaths.h"
#include "demi/schema/Validation.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace demi::editor {
namespace {

bool containsCaseInsensitive(const std::string_view value,
                             const std::string_view filter) {
  if (filter.empty())
    return true;
  const auto lower = [](const unsigned char character) {
    return static_cast<char>(std::tolower(character));
  };
  std::string haystack(value);
  std::string needle(filter);
  std::ranges::transform(haystack, haystack.begin(), lower);
  std::ranges::transform(needle, needle.begin(), lower);
  return haystack.find(needle) != std::string::npos;
}

void drawViewport(EditorWorkspace &workspace, const ImVec2 position,
                  const ImVec2 size, EditorViewportArea &viewportArea) {
  beginEditorPanel("Viewport", position, size,
                   ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse |
                       ImGuiWindowFlags_NoBackground);
  ImGui::TextUnformatted("SCENE VIEW");
  ImGui::SameLine();
  auto &sceneView = workspace.sceneView();
  const bool perspective =
      sceneView.projection() == EditorProjection::Perspective;
  if (ImGui::SmallButton(perspective ? "Perspective" : "Orthographic"))
    sceneView.setProjection(perspective ? EditorProjection::Orthographic
                                        : EditorProjection::Perspective);
  ImGui::SameLine();
  if (ImGui::SmallButton("Frame selected"))
    (void)sceneView.frameEntity(workspace.project().world,
                                workspace.selectedEntityId());
  ImGui::SameLine();
  if (ImGui::SmallButton("Align to camera"))
    (void)sceneView.alignToFirstCamera(workspace.project().world);
  ImGui::SameLine();
  if (ImGui::SmallButton("Reset view"))
    sceneView.reset(workspace.project().world);
  ImGui::Separator();

  const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float canvasWidth = std::max(available.x, 0.0F);
  const float canvasHeight = std::max(available.y, 0.0F);
  const ImVec2 canvasMax{canvasMin.x + canvasWidth, canvasMin.y + canvasHeight};
  viewportArea = {};
  if (canvasWidth >= 1.0F && canvasHeight >= 1.0F) {
    viewportArea = {
        .x =
            static_cast<std::uint16_t>(std::clamp(canvasMin.x, 0.0F, 65535.0F)),
        .y =
            static_cast<std::uint16_t>(std::clamp(canvasMin.y, 0.0F, 65535.0F)),
        .width =
            static_cast<std::uint16_t>(std::clamp(canvasWidth, 1.0F, 65535.0F)),
        .height = static_cast<std::uint16_t>(
            std::clamp(canvasHeight, 1.0F, 65535.0F))};
  }
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const ImVec2 gizmo{canvasMax.x - 66.0F, canvasMin.y + 66.0F};
  draw->AddCircleFilled(gizmo, 5.0F, EditorAccent);
  draw->AddLine(gizmo, {gizmo.x + 35.0F, gizmo.y - 11.0F},
                IM_COL32(239, 79, 104, 255), 3.0F);
  draw->AddLine(gizmo, {gizmo.x - 12.0F, gizmo.y - 37.0F},
                IM_COL32(91, 215, 125, 255), 3.0F);
  draw->AddLine(gizmo, {gizmo.x + 14.0F, gizmo.y + 29.0F},
                IM_COL32(78, 126, 246, 255), 3.0F);
  const runtime::Entity *selected = workspace.selectedEntity();
  const std::string label = selected == nullptr ? "No entity selected"
                                                : "Selected: " + selected->name;
  draw->AddText({canvasMin.x + 16.0F, canvasMin.y + 15.0F},
                IM_COL32(224, 227, 235, 255), label.c_str());
  if (canvasWidth >= 1.0F && canvasHeight >= 1.0F) {
    ImGui::InvisibleButton("viewport-canvas", {canvasWidth, canvasHeight});
    const bool hovered = ImGui::IsItemHovered();
    const bool focused = ImGui::IsWindowFocused();
    ImGuiIO &io = ImGui::GetIO();
    if (focused && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false))
      (void)sceneView.frameEntity(workspace.project().world,
                                  workspace.selectedEntityId());
    sceneView.update({
        .deltaSeconds = io.DeltaTime,
        .mousePosition = {io.MousePos.x - canvasMin.x,
                          io.MousePos.y - canvasMin.y},
        .mouseDelta = {io.MouseDelta.x, io.MouseDelta.y},
        .wheel = hovered ? io.MouseWheel : 0.0F,
        .hovered = hovered,
        .focused = focused && !io.WantTextInput,
        .orbitButton = ImGui::IsMouseDown(ImGuiMouseButton_Left),
        .panButton = ImGui::IsMouseDown(ImGuiMouseButton_Middle),
        .flyButton = ImGui::IsMouseDown(ImGuiMouseButton_Right),
        .orbitModifier = io.KeyAlt,
        .moveForward = ImGui::IsKeyDown(ImGuiKey_W),
        .moveBackward = ImGui::IsKeyDown(ImGuiKey_S),
        .moveLeft = ImGui::IsKeyDown(ImGuiKey_A),
        .moveRight = ImGui::IsKeyDown(ImGuiKey_D),
        .moveUp = ImGui::IsKeyDown(ImGuiKey_E),
        .moveDown = ImGui::IsKeyDown(ImGuiKey_Q),
        .fast = io.KeyShift,
    });
    if (hovered)
      ImGui::SetTooltip("Alt+Left orbit | Middle pan | Wheel zoom | "
                        "Right+WASDQE fly | F frame");
  } else {
    sceneView.update({});
  }
  ImGui::End();
}

void drawConsole(EditorWorkspace &workspace, const ImVec2 position,
                 const ImVec2 size) {
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

const char *sourceKindLabel(const std::filesystem::path &path) {
  switch (classifySourceFile(path)) {
  case SourceFileKind::Project:
    return "PROJECT";
  case SourceFileKind::Scene:
    return "SCENE";
  case SourceFileKind::Hud:
    return "HUD";
  case SourceFileKind::Asset:
    return "ASSET";
  case SourceFileKind::AssetGroup:
    return "GROUP";
  case SourceFileKind::Prefab:
    return "PREFAB";
  case SourceFileKind::UiPrefab:
    return "UI";
  case SourceFileKind::InputReplay:
    return "REPLAY";
  case SourceFileKind::Package:
    return "PACKAGE";
  case SourceFileKind::Save:
    return "SAVE";
  case SourceFileKind::Unknown:
    return "FILE";
  }
  return "FILE";
}

void drawAssets(EditorWorkspace &workspace, const ImVec2 position,
                const ImVec2 size, std::array<char, 128> &filter,
                std::filesystem::path &selectedSource, std::string &notice) {
  beginEditorPanel("Assets", position, size);
  editorSectionTitle("ASSETS", "authored sources");
  ImGui::SetNextItemWidth(-1.0F);
  ImGui::InputTextWithHint("##asset-search", "Search project sources",
                           filter.data(), filter.size());
  if (!selectedSource.empty()) {
    std::error_code error;
    const auto relative = std::filesystem::relative(
        selectedSource, workspace.project().project.projectDirectory, error);
    ImGui::TextDisabled("Selected: %s",
                        (error ? selectedSource.filename() : relative)
                            .generic_string()
                            .c_str());
  }
  ImGui::BeginChild("asset-list", {0.0F, 0.0F}, ImGuiChildFlags_None);
  for (const std::filesystem::path &source : workspace.sources()) {
    const auto relative = std::filesystem::relative(
        source, workspace.project().project.projectDirectory);
    const std::string display = relative.generic_string();
    if (!containsCaseInsensitive(display, filter.data()))
      continue;
    ImGui::PushID(display.c_str());
    ImGui::TextColored({0.54F, 0.40F, 0.85F, 1.0F}, "%s",
                       sourceKindLabel(source));
    ImGui::SameLine(76.0F);
    if (ImGui::Selectable(display.c_str(), selectedSource == source,
                          ImGuiSelectableFlags_SpanAllColumns)) {
      selectedSource = source;
      notice = "Selected project source: " + display;
    }
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", source.string().c_str());
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::End();
}

void drawBuild(const ImVec2 position, const ImVec2 size, bool &linuxTarget,
               bool &androidTarget) {
  beginEditorPanel("Build", position, size);
  editorSectionTitle("BUILD TARGETS");
  ImGui::Checkbox("Linux (64-bit)", &linuxTarget);
  ImGui::Checkbox("Android (ARM64)", &androidTarget);
  ImGui::Spacing();
  ImGui::TextDisabled("Configuration");
  ImGui::SameLine();
  ImGui::TextUnformatted("Debug");
  ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), size.y - 48.0F));
  disabledEditorButton("Build Project",
                       "Build command execution is not connected yet.",
                       {-1.0F, 30.0F});
  ImGui::End();
}

void drawMenu(EditorWorkspace &workspace, const ImVec2 size, bool &wantsExit,
              std::string &notice) {
  beginEditorPanel("MainMenu", {0.0F, 0.0F}, size,
                   ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoScrollbar);
  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Save scene", "Ctrl+S", false,
                          workspace.sceneDocument().isDirty())) {
        std::string error;
        notice = workspace.save(error) ? "Scene saved" : error;
      }
      if (ImGui::MenuItem("Refresh project", "F5")) {
        std::string error;
        notice = workspace.refresh(error) ? "Project refreshed" : error;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Exit")) {
        if (workspace.sceneDocument().isDirty())
          notice = "Save or undo scene changes before exiting.";
        else
          wantsExit = true;
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo", "Ctrl+Z", false,
                          workspace.sceneDocument().canUndo())) {
        std::string error;
        notice = workspace.undo(error) ? "Undid scene edit" : error;
      }
      if (ImGui::MenuItem("Redo", "Ctrl+Y", false,
                          workspace.sceneDocument().canRedo())) {
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
    ImGui::MenuItem("Tools");
    ImGui::MenuItem("Build");
    ImGui::MenuItem("Help");
    ImGui::EndMenuBar();
  }
  ImGui::End();
}

void drawToolbar(const ImVec2 position, const ImVec2 size,
                 EditorWorkspace &workspace, EditorPlaySession &playSession,
                 std::string &notice) {
  beginEditorPanel("Toolbar", position, size, ImGuiWindowFlags_NoScrollbar);
  if (ImGui::Button("Refresh")) {
    std::string error;
    notice = workspace.refresh(error) ? "Project refreshed" : error;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!workspace.sceneDocument().isDirty());
  if (ImGui::Button("Save")) {
    std::string error;
    notice = workspace.save(error) ? "Scene saved" : error;
  }
  ImGui::EndDisabled();
  ImGui::SameLine(170.0F);
  ImGui::BeginDisabled(!workspace.sceneDocument().canUndo());
  if (ImGui::Button("Undo")) {
    std::string error;
    notice = workspace.undo(error) ? "Undid scene edit" : error;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!workspace.sceneDocument().canRedo());
  if (ImGui::Button("Redo")) {
    std::string error;
    notice = workspace.redo(error) ? "Redid scene edit" : error;
  }
  ImGui::EndDisabled();
  const float center = size.x * 0.5F - 88.0F;
  ImGui::SameLine(std::max(center, ImGui::GetCursorPosX() + 20.0F));
  ImGui::PushStyleColor(ImGuiCol_Button, {0.31F, 0.21F, 0.52F, 1.0F});
  ImGui::BeginDisabled(playSession.isRunning());
  if (ImGui::Button("Play")) {
    std::string error;
    if (workspace.sceneDocument().isDirty() && !workspace.save(error)) {
      notice = error;
    } else if (playSession.start(workspace.projectPath(), error)) {
      notice = "Play session started in a runtime window";
    } else {
      notice = error;
    }
  }
  ImGui::EndDisabled();
  ImGui::PopStyleColor();
  ImGui::SameLine();
  ImGui::BeginDisabled(!playSession.isRunning());
  if (ImGui::Button(playSession.isPaused() ? "Resume" : "Pause")) {
    std::string error;
    notice = playSession.togglePause(error)
                 ? (playSession.isPaused() ? "Play session paused"
                                           : "Play session resumed")
                 : error;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  disabledEditorButton("Step",
                       "Frame stepping requires an embedded runtime session.");
  ImGui::SameLine();
  ImGui::BeginDisabled(!playSession.isRunning());
  if (ImGui::Button("Stop")) {
    playSession.stop();
    notice = "Play session stopped";
  }
  ImGui::EndDisabled();
  ImGui::SameLine(size.x - 205.0F);
  ImGui::TextDisabled("Grid 1   Angle 15 deg");
  ImGui::End();
}

void drawStatus(EditorWorkspace &workspace, const ImVec2 position,
                const ImVec2 size, const std::string_view renderer,
                const std::string &notice) {
  beginEditorPanel("Status", position, size,
                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoInputs);
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  draw->AddCircleFilled({cursor.x + 3.0F, cursor.y + 7.0F}, 4.0F, EditorAccent);
  ImGui::SetCursorPosX(20.0F);
  ImGui::Text("%s %s", EngineName.data(), EngineVersion.data());
  ImGui::SameLine(190.0F);
  ImGui::TextDisabled("%.*s", static_cast<int>(renderer.size()),
                      renderer.data());
  if (!notice.empty()) {
    ImGui::SameLine(360.0F);
    ImGui::TextDisabled("%s", notice.c_str());
  }
  const std::string project = "Project: " + workspace.project().project.name;
  ImGui::SameLine(std::max(size.x - 390.0F, 420.0F));
  ImGui::TextDisabled("%s", project.c_str());
  ImGui::SameLine(size.x - 105.0F);
  if (workspace.sceneDocument().isDirty())
    ImGui::TextColored({0.95F, 0.67F, 0.28F, 1.0F}, "Modified");
  else
    ImGui::TextColored({0.32F, 0.86F, 0.49F, 1.0F}, "Ready");
  ImGui::End();
}

} // namespace

EditorShell::EditorShell(EditorWorkspace &workspace) : workspace_(workspace) {}

void EditorShell::draw(const int width, const int height,
                       const std::string_view rendererName) {
  playSession_.poll();
  ImGuiIO &input = ImGui::GetIO();
  if (!input.WantTextInput && input.KeyCtrl &&
      ImGui::IsKeyPressed(ImGuiKey_S, false)) {
    std::string error;
    notice_ = workspace_.save(error) ? "Scene saved" : error;
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
  constexpr float menuHeight = 30.0F;
  constexpr float toolbarHeight = 48.0F;
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

  drawMenu(workspace_, {screenWidth, menuHeight}, wantsExit_, notice_);
  drawToolbar({0.0F, menuHeight}, {screenWidth, toolbarHeight}, workspace_,
              playSession_, notice_);
  hierarchyPanel_.draw(workspace_, {0.0F, contentTop}, {leftWidth, upperHeight},
                       notice_);
  drawViewport(workspace_, {leftWidth, contentTop}, {centerWidth, upperHeight},
               viewportArea_);
  drawInspectorPanel(workspace_, {screenWidth - rightWidth, contentTop},
                     {rightWidth, contentBottom - contentTop}, notice_);
  drawConsole(workspace_, {0.0F, contentTop + upperHeight},
              {consoleWidth, bottomHeight});
  drawAssets(workspace_, {consoleWidth, contentTop + upperHeight},
             {assetsWidth, bottomHeight}, assetFilter_, selectedSource_,
             notice_);
  drawBuild({consoleWidth + assetsWidth, contentTop + upperHeight},
            {buildWidth, bottomHeight}, linuxTarget_, androidTarget_);
  drawStatus(workspace_, {0.0F, contentBottom}, {screenWidth, statusHeight},
             rendererName, notice_);
  conflictPanel_.draw(workspace_, notice_);
}

} // namespace demi::editor
