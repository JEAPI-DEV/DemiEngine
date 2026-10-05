#include "editor/EditorDockingWorkspace.h"
#include "editor/EditorModulesPanel.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace demi::editor;
namespace fs = std::filesystem;

void require(const bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

struct TestDirectory {
  fs::path root;

  TestDirectory() {
    const std::string pattern =
        (fs::temp_directory_path() / "demi-editor-docking-ui-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const char *created = ::mkdtemp(buffer.data());
    require(created != nullptr, "Could not create docking UI test directory");
    root = created;
  }

  ~TestDirectory() {
    std::error_code error;
    fs::remove_all(root, error);
    if (error)
      std::cerr << "Could not clean docking UI fixture: " << error.message()
                << '\n';
  }

  TestDirectory(const TestDirectory &) = delete;
  TestDirectory &operator=(const TestDirectory &) = delete;
};

void initializeImGui() {
  ImGui::SetAllocatorFunctions(
      [](std::size_t size, void *) { return std::malloc(size); },
      [](void *memory, void *) { std::free(memory); });
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.DisplaySize = {1600, 1000};
  io.DeltaTime = 1.0F / 60.0F;
  io.Fonts->AddFontDefault();
  unsigned char *pixels = nullptr;
  int width = 0;
  int height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  require(pixels != nullptr, "Could not build docking test font atlas");
}

void drawPanels(EditorDockingWorkspace &docking) {
  docking.drawDockspace({0, 0}, ImGui::GetIO().DisplaySize);
  for (const EditorPanelDefinition &panel : editorPanelDefinitions()) {
    if (!(docking.visibility().*panel.visible))
      continue;
    ImGui::Begin(panel.windowName.data());
    ImGui::TextUnformatted(panel.windowName.data());
    ImGui::End();
  }
}

template <typename Draw> void renderFrame(Draw &&draw) {
  ImGui::NewFrame();
  draw();
  ImGui::Render();
  require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame == 0,
          "Docking frame produced an ImGui error");
}

ImGuiWindow &window(const char *name) {
  auto *result = ImGui::FindWindowByName(name);
  require(result != nullptr,
          std::string("Missing independent window: ") + name);
  return *result;
}

void checkIndependentDocking(EditorDockingWorkspace &docking) {
  for (int frame = 0; frame < 3; ++frame)
    renderFrame([&] { drawPanels(docking); });

  const ImGuiID authoringDock = window("Viewport").DockId;
  const ImGuiID propertiesDock = window("Inspector").DockId;
  const ImGuiID diagnosticsDock = window("Console").DockId;
  require(authoringDock != 0 && propertiesDock != 0 && diagnosticsDock != 0,
          "Default layout did not dock the independent panels");
  std::set<ImGuiID> windowIds;
  for (const auto &panel : editorPanelDefinitions()) {
    auto &panelWindow = window(panel.windowName.data());
    require(windowIds.insert(panelWindow.ID).second,
            "Two panel names resolve to one window identity");
    if (panel.dockGroup == EditorPanelDockGroup::Authoring)
      require(panelWindow.DockId == authoringDock,
              "Authoring windows are not grouped as default docking tabs");
    if (panel.dockGroup == EditorPanelDockGroup::Properties)
      require(panelWindow.DockId == propertiesDock,
              "Property panels are not grouped as default docking tabs");
    if (panel.dockGroup == EditorPanelDockGroup::Diagnostics)
      require(panelWindow.DockId == diagnosticsDock,
              "Diagnostic panels are not grouped as default docking tabs");
  }

  ImGui::DockBuilderDockWindow("Terrain Graph", propertiesDock);
  ImGui::DockBuilderDockWindow("UI Palette", diagnosticsDock);
  ImGui::DockBuilderFinish(ImHashStr("DemiMainDockspace"));
  for (int frame = 0; frame < 2; ++frame)
    renderFrame([&] { drawPanels(docking); });
  require(window("Terrain Graph").DockId == propertiesDock &&
              window("Viewport").DockId == authoringDock &&
              window("Game View").DockId == authoringDock,
          "Moving Terrain Graph also moved another authoring view");
  require(window("UI Palette").DockId == diagnosticsDock &&
              window("Inspector").DockId == propertiesDock &&
              window("Terrain Nodes").DockId == propertiesDock,
          "Moving UI Palette also moved Inspector or Terrain Nodes");

  docking.visibility().luaConsole = false;
  docking.visibility().terrainGraph = false;
  renderFrame([&] { drawPanels(docking); });
  require(!window("Lua Console").Active && window("Console").Active &&
              !window("Terrain Graph").Active && window("Viewport").Active,
          "Closing a panel hid its siblings");
  docking.requestReset();
  renderFrame([&] { drawPanels(docking); });
  require(docking.visibility() == EditorPanelVisibility{},
          "Reset did not restore panel visibility");
  require(window("Terrain Graph").DockId == window("Viewport").DockId &&
              window("UI Palette").DockId == window("Inspector").DockId,
          "Reset did not restore the default real docking groups");
}

void checkPaletteContexts() {
  require(editorPaletteIncludesKind(EditorModuleKind::HudElement,
                                    EditorModuleKind::HudElement) &&
              editorPaletteIncludesKind(EditorModuleKind::HudElement,
                                        EditorModuleKind::UiPrefab) &&
              !editorPaletteIncludesKind(EditorModuleKind::HudElement,
                                         EditorModuleKind::TerrainNode),
          "HUD palette does not isolate controls and UI prefabs");
  require(editorPaletteIncludesKind(EditorModuleKind::UiPrefab,
                                    EditorModuleKind::HudElement) &&
              editorPaletteIncludesKind(EditorModuleKind::UiPrefab,
                                        EditorModuleKind::UiPrefab) &&
              !editorPaletteIncludesKind(EditorModuleKind::UiPrefab,
                                         EditorModuleKind::TerrainNode),
          "UI prefab editing does not use the HUD palette context");
  require(editorPaletteIncludesKind(EditorModuleKind::TerrainNode,
                                    EditorModuleKind::TerrainNode) &&
              !editorPaletteIncludesKind(EditorModuleKind::TerrainNode,
                                         EditorModuleKind::HudElement) &&
              !editorPaletteIncludesKind(EditorModuleKind::TerrainNode,
                                         EditorModuleKind::UiPrefab),
          "Terrain node palette includes UI modules");

  EditorWorkspace workspace;
  require(workspace.activeDocument() == EditorWorkspaceDocument::Scene,
          "Explicit palette context test requires a non-HUD workspace");
  EditorModulesPanelState ui;
  EditorModulesPanelState terrain;
  // Explicit context controls the palette independently of activeDocument().
  ui.catalog = editorModules(workspace);
  terrain.catalog = ui.catalog;
  bool uiOpen = true;
  bool terrainOpen = true;
  renderFrame([&] {
    drawEditorPalettePanel(workspace, ui, EditorModuleKind::HudElement,
                           &uiOpen);
    drawEditorPalettePanel(workspace, terrain, EditorModuleKind::TerrainNode,
                           &terrainOpen);
  });
  require(window("UI Palette").Active && window("Terrain Nodes").Active,
          "Contextual palettes were not rendered as separate windows");
  renderFrame([&] {
    drawEditorPalettePanel(workspace, terrain, EditorModuleKind::TerrainNode,
                           &terrainOpen);
  });
  require(!window("UI Palette").Active && uiOpen,
          "Leaving HUD context changed its saved open preference");
}

} // namespace

int main() {
  try {
    TestDirectory fixture;
    initializeImGui();
    EditorDockingWorkspace docking(fixture.root);
    checkIndependentDocking(docking);
    checkPaletteContexts();
    ImGui::DestroyContext();
    std::cout << "Independent docking and palette contexts passed\n";
    return 0;
  } catch (const std::exception &error) {
    if (ImGui::GetCurrentContext() != nullptr)
      ImGui::DestroyContext();
    std::cerr << error.what() << '\n';
    return 1;
  }
}
