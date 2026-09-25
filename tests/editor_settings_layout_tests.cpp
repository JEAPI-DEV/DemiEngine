#include "cli/project/ProjectTemplates.h"
#include "editor/EditorBuildPanel.h"
#include "editor/EditorPreferencesStore.h"
#include "editor/EditorProjectPanel.h"
#include "editor/EditorSettingsPanel.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cassert>
#include <cstdlib>
#include <filesystem>

namespace {
void *allocate(std::size_t bytes, void *) { return std::malloc(bytes); }
void release(void *memory, void *) { std::free(memory); }

void checkPanels(demi::editor::EditorWorkspace &workspace, ImVec2 screen) {
  ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = screen;
  io.DeltaTime = 1.0F / 60.0F;
  ImFontConfig font;
  font.SizePixels = 18;
  io.Fonts->AddFontDefault(&font);
  unsigned char *pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  assert(pixels && width > 0 && height > 0);
  demi::editor::EditorProjectPanel project;
  demi::editor::EditorBuildPanel build;
  demi::editor::EditorPreferences preferences;
  project.openSettings();
  build.open();
  bool open = true;
  float scale = 1;
  std::string notice;
  const auto originalProject = workspace.projectDocument().json();
  for (int frame = 0; frame < 3; ++frame) {
    ImGui::NewFrame();
    demi::editor::drawEditorSettingsPanel(open, scale, preferences);
    project.draw(workspace, notice);
    build.draw(workspace, notice);
    for (const char *name : {"Editor Settings", "Project Settings", "Build Project"}) {
      const auto *window = ImGui::FindWindowByName(name);
      assert(window && window->Active);
      assert(window->Size.x <= screen.x && window->Size.y <= screen.y);
      assert(window->Size.x >= 500 && window->Size.y >= 350);
    }
    ImGui::Render();
    assert(ImGui::GetDrawData()->TotalVtxCount > 0);
  }
  assert(workspace.projectDocument().json() == originalProject);
  ImGui::DestroyContext();
}
}

int main() {
  namespace fs = std::filesystem;
  const auto root = fs::temp_directory_path() / "demi_settings_layout_tests";
  std::error_code ignored;
  fs::remove_all(root, ignored);
  demi::Diagnostics diagnostics;
  demi::cli::project::ProjectTemplateCatalog catalog(fs::path(DEMI_SOURCE_DIR) / "templates");
  const auto starter = catalog.find("blank-2d", diagnostics);
  assert(starter);
  assert(demi::cli::project::ProjectScaffolder{}.create(
      {.projectTemplate = *starter, .destination = root, .projectName = "Settings Layout"}).committed);
  demi::editor::EditorWorkspace workspace;
  std::string error;
  assert(workspace.open(root, error));
  ImGui::SetAllocatorFunctions(allocate, release);
  checkPanels(workspace, {1920, 1080});
  checkPanels(workspace, {800, 600});
  fs::remove_all(root, ignored);
}
