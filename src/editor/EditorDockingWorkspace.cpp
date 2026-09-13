#include "editor/EditorDockingWorkspace.h"

#include "editor/EditorDefaultLayout.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <utility>

namespace demi::editor {

namespace {

constexpr const char *DockspaceHost = "Demi Workbench###DemiDockspaceHost";
constexpr const char *HierarchyWindow = "Hierarchy";
constexpr const char *StageWindow = "Stage";
constexpr const char *InspectorWindow = "Inspector";
constexpr const char *ConsoleWindow = "Console";
constexpr const char *AssetsWindow = "Assets";
constexpr const char *SpecializedWindow = "Specialized Document";
constexpr const char *AnimationWindow = "Animation State Machine";

} // namespace

EditorDockingWorkspace::EditorDockingWorkspace(
    std::filesystem::path editorDataRoot)
    : store_(std::move(editorDataRoot)) {
  if (!store_.loadVisibility(visibility_, diagnostic_))
    visibility_ = {};
  savedVisibility_ = visibility_;
}

void EditorDockingWorkspace::drawDockspace(const ImVec2 position,
                                           const ImVec2 size) {
  ImGui::SetNextWindowPos(position, ImGuiCond_Always);
  ImGui::SetNextWindowSize(size, ImGuiCond_Always);
  ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0.0F, 0.0F});
  constexpr ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoBackground;
  ImGui::Begin(DockspaceHost, nullptr, flags);
  ImGui::PopStyleVar(3);

  const ImGuiID dockspaceId = ImHashStr("DemiMainDockspace");
  const bool explicitReset = resetRequested_;
  if (explicitReset || ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
    buildDefaultLayout(dockspaceId, size);
    resetRequested_ = false;
    if (explicitReset && ImGui::GetIO().IniFilename != nullptr)
      ImGui::SaveIniSettingsToDisk(ImGui::GetIO().IniFilename);
  }
  ImGui::DockSpace(dockspaceId, {0.0F, 0.0F},
                   ImGuiDockNodeFlags_PassthruCentralNode);
  ImGui::End();
}

void EditorDockingWorkspace::drawViewMenu() {
  bool changed = false;
  changed |= ImGui::MenuItem("Hierarchy", nullptr, &visibility_.hierarchy);
  changed |= ImGui::MenuItem("Stage", nullptr, &visibility_.stage);
  changed |= ImGui::MenuItem("Inspector", nullptr, &visibility_.inspector);
  changed |= ImGui::MenuItem("Console", nullptr, &visibility_.console);
  changed |= ImGui::MenuItem("Assets", nullptr, &visibility_.assets);
  ImGui::Separator();
  if (ImGui::MenuItem("Reset Workspace"))
    requestReset();
  if (changed)
    persistVisibilityIfChanged();
}

void EditorDockingWorkspace::requestReset() {
  visibility_ = {};
  resetRequested_ = true;
  ImGui::ClearIniSettings();
  persistVisibilityIfChanged();
}

void EditorDockingWorkspace::persistVisibilityIfChanged() {
  if (visibility_ == savedVisibility_)
    return;
  std::string error;
  if (store_.saveVisibility(visibility_, error)) {
    savedVisibility_ = visibility_;
  } else {
    diagnostic_ = "Workspace visibility: " + error;
  }
}

EditorPanelVisibility &EditorDockingWorkspace::visibility() noexcept {
  return visibility_;
}

std::string EditorDockingWorkspace::takeDiagnostic() {
  return std::exchange(diagnostic_, {});
}

void EditorDockingWorkspace::buildDefaultLayout(const unsigned int dockspaceId,
                                                const ImVec2 size) {
  ImGui::DockBuilderRemoveNode(dockspaceId);
  ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspaceId, size);

  ImGuiID center = dockspaceId;
  ImGuiID left = 0;
  ImGuiID right = 0;
  ImGuiID bottom = 0;
  left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,
                                     EditorDefaultLayout::HierarchyRatio,
                                     nullptr, &center);
  right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right,
                                      EditorDefaultLayout::InspectorRatio,
                                      nullptr, &center);
  bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down,
                                       EditorDefaultLayout::BottomRatio,
                                       nullptr, &center);
  ImGuiID bottomLeft = 0;
  ImGuiID bottomRight = bottom;
  bottomLeft = ImGui::DockBuilderSplitNode(bottomRight, ImGuiDir_Left,
                                           EditorDefaultLayout::ConsoleRatio,
                                           nullptr, &bottomRight);

  ImGui::DockBuilderDockWindow(HierarchyWindow, left);
  ImGui::DockBuilderDockWindow(StageWindow, center);
  ImGui::DockBuilderDockWindow(InspectorWindow, right);
  ImGui::DockBuilderDockWindow(ConsoleWindow, bottomLeft);
  ImGui::DockBuilderDockWindow(AssetsWindow, bottomRight);
  ImGui::DockBuilderDockWindow(SpecializedWindow, center);
  ImGui::DockBuilderDockWindow(AnimationWindow, center);
  ImGui::DockBuilderFinish(dockspaceId);
}

} // namespace demi::editor
