#include "editor/EditorDockingWorkspace.h"

#include "editor/EditorDefaultLayout.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <utility>

namespace demi::editor {

namespace {

constexpr const char *DockspaceHost = "Demi Workbench###DemiDockspaceHost";
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

void EditorDockingWorkspace::drawViewMenu(const bool uiPaletteAvailable,
                                          const bool terrainNodesAvailable) {
  bool changed = false;
  EditorPanelDockGroup previousGroup = EditorPanelDockGroup::Hierarchy;
  for (const EditorPanelDefinition &panel : editorPanelDefinitions()) {
    if (panel.dockGroup != previousGroup)
      ImGui::Separator();
    previousGroup = panel.dockGroup;
    const bool available =
        panel.availability == EditorPanelAvailability::Always ||
        (panel.availability == EditorPanelAvailability::Hud &&
         uiPaletteAvailable) ||
        (panel.availability == EditorPanelAvailability::TerrainGraph &&
         terrainNodesAvailable);
    changed |= ImGui::MenuItem(panel.windowName.data(), nullptr,
                               &(visibility_.*panel.visible), available);
    if (!available &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
      ImGui::SetTooltip("Available while editing %s.",
                        panel.availability == EditorPanelAvailability::Hud
                            ? "a HUD or UI prefab"
                            : "a terrain graph");
    }
  }
  ImGui::Separator();
  if (ImGui::MenuItem("Reset Workspace"))
    requestReset();
  if (changed)
    persistVisibilityIfChanged();
}

bool EditorDockingWorkspace::focusPanel(std::string_view windowName) {
  const std::string name(windowName);
  auto *window = ImGui::FindWindowByName(name.c_str());
  if (!window || !window->Active)
    return false;
  ImGui::FocusWindow(window, ImGuiFocusRequestFlags_UnlessBelowModal);
  return ImGui::GetCurrentContext()->NavWindow == window && !window->Hidden &&
         !window->SkipItems && (!window->DockNode || window->DockTabIsVisible);
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
  const float inspectorFraction = EditorDefaultLayout::InspectorRatio *
                                  (1.0F - EditorDefaultLayout::HierarchyRatio);
  right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, inspectorFraction,
                                      nullptr, &center);
  bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down,
                                       EditorDefaultLayout::BottomRatio,
                                       nullptr, &center);
  left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,
                                     EditorDefaultLayout::HierarchyRatio /
                                         (1.0F - inspectorFraction),
                                     nullptr, &center);
  ImGuiID bottomLeft = 0;
  ImGuiID bottomRight = bottom;
  bottomLeft = ImGui::DockBuilderSplitNode(bottomRight, ImGuiDir_Left,
                                           EditorDefaultLayout::ConsoleRatio,
                                           nullptr, &bottomRight);

  for (const EditorPanelDefinition &panel : editorPanelDefinitions()) {
    ImGuiID destination = center;
    switch (panel.dockGroup) {
    case EditorPanelDockGroup::Hierarchy:
      destination = left;
      break;
    case EditorPanelDockGroup::Authoring:
      destination = center;
      break;
    case EditorPanelDockGroup::Properties:
      destination = right;
      break;
    case EditorPanelDockGroup::Diagnostics:
      destination = bottomLeft;
      break;
    case EditorPanelDockGroup::Assets:
      destination = bottomRight;
      break;
    }
    ImGui::DockBuilderDockWindow(panel.windowName.data(), destination);
  }
  ImGui::DockBuilderDockWindow(SpecializedWindow, center);
  ImGui::DockBuilderDockWindow(AnimationWindow, center);
  ImGui::DockBuilderFinish(dockspaceId);
}

} // namespace demi::editor
