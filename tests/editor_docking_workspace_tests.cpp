#include "editor/EditorDockingState.h"
#include "editor/EditorDockingWorkspace.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cassert>
#include <cstdlib>
#include <filesystem>

namespace {

void *allocate(const std::size_t size, void *) { return std::malloc(size); }
void release(void *memory, void *) { std::free(memory); }

} // namespace

int main() {
  namespace fs = std::filesystem;
  using namespace demi::editor;

  const fs::path root =
      fs::temp_directory_path() / "demi_editor_docking_workspace_tests";
  std::error_code cleanupError;
  fs::remove_all(root, cleanupError);

  ImGui::SetAllocatorFunctions(allocate, release);
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.DisplaySize = {1280.0F, 720.0F};
  io.DeltaTime = 1.0F / 60.0F;
  unsigned char *fontPixels = nullptr;
  int fontWidth = 0;
  int fontHeight = 0;
  io.Fonts->GetTexDataAsRGBA32(&fontPixels, &fontWidth, &fontHeight);
  assert(fontPixels != nullptr && fontWidth > 0 && fontHeight > 0);

  EditorDockingWorkspace workspace(root);
  ImGui::NewFrame();
  workspace.drawDockspace({0.0F, 84.0F}, {1280.0F, 609.0F});
  const ImGuiID dockspaceId = ImHashStr("DemiMainDockspace");
  assert(ImGui::DockBuilderGetNode(dockspaceId) != nullptr);
  workspace.visibility().assets = false;
  workspace.persistVisibilityIfChanged();
  ImGui::Render();

  ImGui::NewFrame();
  workspace.requestReset();
  io.DisplaySize = {640.0F, 360.0F};
  workspace.drawDockspace({0.0F, 84.0F}, {640.0F, 249.0F});
  assert(workspace.visibility() == EditorPanelVisibility{});
  assert(ImGui::DockBuilderGetNode(dockspaceId) != nullptr);
  ImGui::Render();
  ImGui::DestroyContext();

  EditorDockingStateStore store(root);
  EditorPanelVisibility restored;
  std::string error;
  assert(store.loadVisibility(restored, error));
  assert(restored == EditorPanelVisibility{});

  fs::remove_all(root, cleanupError);
  return 0;
}
