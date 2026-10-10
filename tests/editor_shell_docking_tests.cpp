#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include "editor/EditorDragDropPayloads.h"
#include "editor/EditorRecoveryStore.h"
#include "editor/EditorShell.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;
using namespace demi::editor;

void require(const bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

class TestDirectory {
public:
  TestDirectory() {
    const std::string pattern =
        (fs::temp_directory_path() / "demi-editor-shell-docking-XXXXXX")
            .string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const char *created = ::mkdtemp(buffer.data());
    require(created != nullptr, "Could not create Shell test directory");
    root = created;
  }

  ~TestDirectory() {
    std::error_code error;
    fs::remove_all(root, error);
    if (error)
      std::cerr << "Could not clean Shell fixture: " << error.message() << '\n';
  }

  TestDirectory(const TestDirectory &) = delete;
  TestDirectory &operator=(const TestDirectory &) = delete;
  fs::path root;
};

class ScopedEnvironment {
public:
  ScopedEnvironment(std::string name, const fs::path &value)
      : name_(std::move(name)) {
    if (const char *previous = std::getenv(name_.c_str()))
      previous_ = previous;
    require(::setenv(name_.c_str(), value.c_str(), 1) == 0,
            "Could not isolate " + name_);
  }

  ~ScopedEnvironment() {
    const int result = previous_
                           ? ::setenv(name_.c_str(), previous_->c_str(), 1)
                           : ::unsetenv(name_.c_str());
    if (result != 0)
      std::cerr << "Could not restore " << name_ << '\n';
  }

  ScopedEnvironment(const ScopedEnvironment &) = delete;
  ScopedEnvironment &operator=(const ScopedEnvironment &) = delete;

private:
  std::string name_;
  std::optional<std::string> previous_;
};

void writeJson(const fs::path &path, const Json &value) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path);
  output << value.dump(2) << '\n';
  require(output.good(), "Could not write fixture: " + path.string());
}

void createProject(const fs::path &root) {
  writeJson(root / "demi.project.json",
            {{"format_version", 1},
             {"name", "Shell docking probe"},
             {"main_scene", "scene://shell/main"},
             {"scenes", Json::array({{{"id", "scene://shell/main"},
                                      {"path", "scenes/main.scene.json"}}})}});
  writeJson(
      root / "scenes/main.scene.json",
      {{"format_version", 1},
       {"id", "scene://shell/main"},
       {"hud", "main.hud.json"},
       {"entities",
        Json::array({{{"id", "player"},
                      {"components", {{"Transform2D", Json::object()}}}}})}});
  writeJson(
      root / "scenes/main.hud.json",
      {{"format_version", 1},
       {"canvas_size", {800, 600}},
       {"children",
        Json::array(
            {{{"id", "status"}, {"type", "label"}, {"text", "Shell HUD probe"}},
             {{"id", "other"}, {"type", "label"}, {"text", "Other label"}}})}});
}

class ImGuiFixture {
public:
  ImGuiFixture() {
    ImGui::SetAllocatorFunctions(
        [](std::size_t bytes, void *) { return std::malloc(bytes); },
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
    require(pixels && width > 0 && height > 0,
            "Could not build Shell test font atlas");
    auto *context = ImGui::GetCurrentContext();
    context->ErrorCallback = [](ImGuiContext *, void *userData,
                                const char *message) {
      static_cast<std::vector<std::string> *>(userData)->emplace_back(message);
    };
    context->ErrorCallbackUserData = &warnings_;
  }

  ~ImGuiFixture() { ImGui::DestroyContext(); }

  void frame(EditorShell &shell, const char *focus = nullptr,
             const std::function<void()> &beforeDraw = {}) {
    ImGui::NewFrame();
    if (focus)
      ImGui::SetWindowFocus(focus);
    if (beforeDraw)
      beforeDraw();
    shell.draw(1600, 1000, "Synthetic ImGui");
    ImGui::Render();
    const auto *context = ImGui::GetCurrentContext();
    require(warnings_.empty() && context->ErrorCountCurrentFrame == 0,
            "Shell frame produced an ImGui warning: " +
                (warnings_.empty() ? std::string("scope/layout error")
                                   : warnings_.front()));
    require(context->CurrentTabBarStack.empty(),
            "Shell left an internal tab bar scope open");
    require(ImGui::GetDrawData() != nullptr,
            "Shell frame produced no draw data");
  }

private:
  std::vector<std::string> warnings_;
};

ImGuiWindow &window(const char *name) {
  auto *result = ImGui::FindWindowByName(name);
  require(result != nullptr, std::string("Missing Shell window: ") + name);
  return *result;
}

bool active(const char *name) {
  const auto *result = ImGui::FindWindowByName(name);
  return result && result->Active;
}

std::string describeDockingWindow(const char *name) {
  const auto *view = ImGui::FindWindowByName(name);
  if (!view)
    return std::string(name) + ": missing";
  std::ostringstream output;
  output << name << ": id=" << view->ID << " tab=" << view->TabId
         << " dock=" << view->DockId << " flags=" << view->Flags
         << " active=" << view->Active << " hidden=" << view->Hidden
         << " skip=" << view->SkipItems
         << " dock-tab-visible=" << view->DockTabIsVisible;
  if (const auto *node = view->DockNode) {
    output << " node-selected=" << node->SelectedTabId;
    if (const auto *tabs = node->TabBar)
      output << " selected=" << tabs->SelectedTabId
             << " next-selected=" << tabs->NextSelectedTabId
             << " visible=" << tabs->VisibleTabId;
    else
      output << " tab-bar=missing";
  }
  return output.str();
}

void checkFreshTerrainGraphStartupFocus() {
  TestDirectory fixture;
  ScopedEnvironment data("XDG_DATA_HOME", fixture.root / "data");
  ScopedEnvironment cache("XDG_CACHE_HOME", fixture.root / "cache");
  const auto project = fixture.root / "project";
  demi::runtime::TerrainRecipe recipe;
  recipe.size = {4, 4};
  recipe.cellsX = recipe.cellsZ = 4;
  recipe.chunkCells = 4;
  recipe.graph = demi::runtime::defaultTerrainGraph();
  writeJson(project / "demi.project.json",
            {{"format_version", 1},
             {"name", "Fresh terrain graph focus"},
             {"main_scene", "scene://terrain/main"},
             {"scenes", Json::array({{{"id", "scene://terrain/main"},
                                      {"path", "scenes/main.scene.json"}}})}});
  writeJson(
      project / "scenes/main.scene.json",
      {{"format_version", 1},
       {"id", "scene://terrain/main"},
       {"entities",
        Json::array({{{"id", "terrain"},
                      {"components",
                       {{"Transform3D", Json::object()},
                        {"Terrain3D", {{"recipe", recipe.toJson()}}}}}}})}});
  EditorWorkspace workspace;
  std::string error;
  require(workspace.open(project, error), error);
  require(workspace.selectedEntityId() == "terrain",
          "The fresh scene did not select its terrain owner");
  const auto source = workspace.sceneDocument().json();
  ImGuiFixture imgui;
  EditorShell shell(workspace);
  // Matches --terrain-graph: request graph opening before the first dockspace
  // frame. Do not provide a focus argument, simulate a click or amend layout.
  require(shell.openTerrainGraph(error), error);
  std::ostringstream trace;
  for (int frame = 0; frame < 5; ++frame) {
    imgui.frame(shell);
    const auto *context = ImGui::GetCurrentContext();
    trace << "\nFrame " << frame
          << " nav=" << (context->NavWindow ? context->NavWindow->Name : "none")
          << "\n  " << describeDockingWindow("Terrain Graph") << "\n  "
          << describeDockingWindow("Viewport");
  }
  require(active("Terrain Presets") && active("Terrain Nodes"),
          "Terrain graph did not expose its two palettes");
  require(window("Terrain Presets").DockId == window("Inspector").DockId &&
              window("Terrain Presets").ID != window("Terrain Nodes").ID,
          "Terrain Presets is not an independent Inspector sibling");
  require(workspace.terrainAuthoring().presets().size() >= 4,
          "Fresh project has no starter presets");
  const auto &graph = window("Terrain Graph");
  const auto &viewport = window("Viewport");
  require(
      graph.Active && !graph.Hidden && !graph.SkipItems &&
          graph.DockTabIsVisible,
      "Startup graph request did not make Terrain Graph's contents visible" +
          trace.str());
  require(graph.DockNode && graph.DockNode == viewport.DockNode &&
              graph.DockNode->TabBar &&
              graph.DockNode->TabBar->SelectedTabId == graph.TabId,
          "The fresh dock layout selected Viewport instead of Terrain Graph" +
              trace.str());
  require(!viewport.DockTabIsVisible,
          "Viewport retained tab selection after the startup graph request" +
              trace.str());
  require(workspace.sceneDocument().json() == source &&
              !workspace.hasUnsavedChanges(),
          "Opening and focusing the startup graph mutated authored terrain");
  shell.releaseUiResources();
}

void checkIndependentViews(ImGuiFixture &imgui, EditorShell &shell,
                           const fs::path &project) {
  for (int frame = 0; frame < 3; ++frame)
    imgui.frame(shell, "Viewport");
  require(active("Viewport") && active("HUD"),
          "Scene and its HUD did not create independent windows");
  require(!active("UI Palette") && !active("Terrain Nodes") &&
              !active("Terrain Presets"),
          "A scene opened a contextual palette without its editing context");
  for (const char *name : {"Viewport", "HUD"}) {
    const auto &view = window(name);
    // Dear ImGui parents docked windows to its dock host internally. They must
    // not be authored child windows of another editor panel.
    require(
        (view.ParentWindow == nullptr ||
         (view.DockNode && view.ParentWindow == view.DockNode->HostWindow)) &&
            !(view.Flags & ImGuiWindowFlags_NoDocking),
        std::string("Authoring view is not independently dockable: ") + name);
  }
  require(window("Viewport").ID != window("HUD").ID,
          "Scene and HUD share one ImGui window identity");
  require(!active("Stage"), "The old forced Stage wrapper is still active");

  std::string error;
  require(shell.openDocument(project / "scenes/main.hud.json", error), error);
  for (int frame = 0; frame < 3; ++frame)
    imgui.frame(shell, "HUD");
  require(active("UI Palette") && !active("Terrain Nodes") &&
              !active("Terrain Presets"),
          "HUD context did not show only the UI Palette");
  const auto &palette = window("UI Palette");
  require((palette.ParentWindow == nullptr ||
           (palette.DockNode &&
            palette.ParentWindow == palette.DockNode->HostWindow)) &&
              palette.ID != window("Inspector").ID,
          "UI Palette is still nested inside Inspector");

  const ImGuiID dockspace = ImHashStr("DemiMainDockspace");
  const ImGuiID previousDock = window("Viewport").DockId;
  require(previousDock != 0, "Shell did not create its real dockspace");
  ImGuiID sceneDock = 0;
  ImGuiID hudDock = 0;
  ImGui::DockBuilderSplitNode(previousDock, ImGuiDir_Right, 0.5F, &hudDock,
                              &sceneDock);
  ImGui::DockBuilderDockWindow("Viewport", sceneDock);
  ImGui::DockBuilderDockWindow("HUD", hudDock);
  ImGui::DockBuilderFinish(dockspace);
  for (int frame = 0; frame < 3; ++frame)
    imgui.frame(shell, "HUD");
  require(window("Viewport").DockId == sceneDock &&
              window("HUD").DockId == hudDock && sceneDock != hudDock,
          "Scene and HUD could not be docked side by side");
  const auto &scene = shell.authoringViews()[static_cast<std::size_t>(
      EditorAuthoringView::Viewport)];
  const auto &hud =
      shell
          .authoringViews()[static_cast<std::size_t>(EditorAuthoringView::Hud)];
  require(scene.workspace && hud.workspace && scene.area.width &&
              scene.area.height && hud.area.width && hud.area.height,
          "Visible Scene and HUD did not publish simultaneous render requests");
  require(scene.workspace != hud.workspace,
          "Scene and HUD still share one mutable authoring workspace");
  const auto hudSource = hud.workspace->hudDocument()->json();
  require(scene.area.x != hud.area.x || scene.area.y != hud.area.y,
          "Scene and HUD render requests have the same content origin");

  for (int frame = 0; frame < 3; ++frame)
    imgui.frame(shell, "Viewport");
  require(!active("UI Palette") && active("HUD") && active("Viewport"),
          "UI Palette did not follow editing focus independently of HUD "
          "visibility");
  require(hud.workspace->hudDocument()->json() == hudSource,
          "Focusing Scene replaced the independent HUD source");
  for (int frame = 0; frame < 3; ++frame)
    imgui.frame(shell, "HUD");
  require(active("UI Palette"), "Returning to HUD did not restore UI Palette");
  for (const char *tool : {"Inspector", "UI Palette"}) {
    for (int frame = 0; frame < 3; ++frame)
      imgui.frame(shell, tool);
    require(active("UI Palette"), "HUD tool focus hid its contextual palette");
  }
  // Dragging out of Assets does not focus the target ImGui window. The drop
  // itself must select the Scene document instead of retaining HUD Inspector.
  const auto dropSource = project / "prefabs/drop.prefab.json";
  writeJson(
      dropSource,
      {{"format_version", 1},
       {"id", "prefab://drop"},
       {"entities",
        Json::array({{{"id", "body"},
                      {"components", {{"Transform2D", Json::object()}}}}})}});
  const auto payload = dropSource.string();
  const auto dropArea = scene.area;
  auto &io = ImGui::GetIO();
  for (bool held : {true, true, false}) {
    io.AddMousePosEvent(dropArea.x + dropArea.width / 2.0F,
                        dropArea.y + dropArea.height / 2.0F);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, held);
    imgui.frame(shell, "Assets", [&] {
      require(
          ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern |
                                     ImGuiDragDropFlags_SourceNoPreviewTooltip),
          "External prefab drag unavailable");
      ImGui::SetDragDropPayload(EditorPrefabSourcePayload, payload.c_str(),
                                payload.size() + 1);
      ImGui::EndDragDropSource();
    });
  }
  imgui.frame(shell, "Assets");
  require(!shell.showingHudView() && !active("UI Palette"),
          "Delivered Scene drop retained HUD focus");
  require(scene.workspace->selectedEntityId() != "player",
          "Placed prefab was not selected");
  require(scene.workspace->undo(error), error);
  for (int frame = 0; frame < 3; ++frame)
    imgui.frame(shell, "HUD");
  for (int frame = 0; frame < 3; ++frame)
    imgui.frame(shell, "Game View");
  require(!active("UI Palette"), "Idle Game View exposed the HUD palette");
  for (int frame = 0; frame < 3; ++frame)
    imgui.frame(shell, "Inspector");
  require(active("UI Palette"),
          "Returning to authored HUD properties hid its palette");
}

void checkHudTextEditing(ImGuiFixture &imgui, EditorShell &shell,
                         const fs::path &project) {
  auto *workspace =
      shell.authoringViews()[static_cast<std::size_t>(EditorAuthoringView::Hud)]
          .workspace;
  require(workspace != nullptr, "Missing HUD workspace");
  workspace->selectHudNode("status");
  for (int frame = 0; frame < 3; ++frame)
    imgui.frame(shell, "Inspector");
  auto &inspector = window("Inspector");
  inspector.StateStorage.SetInt(inspector.GetID("Layout"), 0);
  inspector.StateStorage.SetInt(inspector.GetID("Appearance"), 0);
  imgui.frame(shell, "Inspector");
  const auto tableId = inspector.GetID("hud-content-properties");
  const auto workspaceId = ImHashData(&workspace, sizeof(workspace), tableId);
  const auto nodeId = ImHashStr("status", 0, workspaceId);
  const auto textId = ImHashStr("##text", 0, nodeId);
  ImGui::ActivateItemByID(textId);
  ImGui::GetCurrentContext()->NavNextActivateFlags =
      ImGuiActivateFlags_PreferInput;
  imgui.frame(shell, "Inspector");
  require(ImGui::GetCurrentContext()->InputTextState.ID == textId,
          "Could not activate HUD Text input");
  auto &input = ImGui::GetIO();
  input.AddInputCharactersUTF8("Power: ");
  imgui.frame(shell, "Inspector");
  input.AddInputCharactersUTF8("12 kW");
  imgui.frame(shell, "Inspector");
  const auto edited =
      workspace->hudDocument()->authoredNode("status")->at("text");
  require(edited.get<std::string>().find("Power: 12 kW") != std::string::npos,
          "Typing without Enter did not update HUD source");
  require(input.WantTextInput, "Text field lost focus before Save all");
  input.AddKeyEvent(ImGuiMod_Ctrl, true);
  input.AddKeyEvent(ImGuiKey_S, true);
  imgui.frame(shell, "Inspector");
  input.AddKeyEvent(ImGuiKey_S, false);
  input.AddKeyEvent(ImGuiMod_Ctrl, false);
  imgui.frame(shell, "Inspector");
  require(!workspace->hudDocument()->isDirty(),
          "Ctrl+S while typing did not save HUD");
  EditorHudDocument reopened;
  std::string error;
  require(reopened.open(project / "scenes/main.hud.json", error), error);
  require(reopened.authoredNode("status")->at("text") == edited,
          "Saved HUD omitted the active text edit");
  const auto beforeSelection = workspace->hudDocument()->json();
  workspace->selectHudNode("other");
  imgui.frame(shell, "Hierarchy");
  require(workspace->hudDocument()->json() == beforeSelection,
          "Selection change rewrote HUD fields");
  input.AddKeyEvent(ImGuiMod_Ctrl, true);
  input.AddKeyEvent(ImGuiKey_Z, true);
  imgui.frame(shell, "Hierarchy");
  input.AddKeyEvent(ImGuiKey_Z, false);
  input.AddKeyEvent(ImGuiMod_Ctrl, false);
  imgui.frame(shell, "Hierarchy");
  require(workspace->hudDocument()->authoredNode("status")->at("text") ==
                  "Shell HUD probe" &&
              !workspace->hudDocument()->canUndo(),
          "A typing session did not undo in one step");
  require(workspace->redo(error), error);
  require(workspace->hudDocument()->authoredNode("status")->at("text") ==
              edited,
          "Redo lost the text edit");
}

void closeDockTab(ImGuiFixture &imgui, EditorShell &shell, const char *name) {
  auto &view = window(name);
  require(view.DockNode && view.HasCloseButton,
          std::string(name) + " has no independent dock tab close control");
  // Middle-click the actual dock tab: exercise Dear ImGui's close control and
  // Shell's open flag rather than mutating private Shell visibility state.
  const ImRect tab = view.DC.DockTabItemRect;
  require(tab.GetWidth() > 0 && tab.GetHeight() > 0,
          std::string(name) + " dock tab has no hit rectangle");
  auto &input = ImGui::GetIO();
  const ImVec2 center = tab.GetCenter();
  input.AddMousePosEvent(center.x, center.y);
  imgui.frame(shell);
  input.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
  imgui.frame(shell);
  input.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
  imgui.frame(shell);
}

void checkHideAndReopen(ImGuiFixture &imgui, EditorShell &shell,
                        const fs::path &project) {
  const auto hudId = window("HUD").ID;
  const auto sceneId = window("Viewport").ID;
  closeDockTab(imgui, shell, "HUD");
  for (int frame = 0; frame < 3; ++frame)
    imgui.frame(shell, "Viewport");
  require(!active("HUD") && active("Viewport") && active("Inspector"),
          "Closing HUD hid another independent panel");
  require(!active("UI Palette"), "A closed HUD left its UI Palette active");
  require(
      shell.authoringViews()[static_cast<std::size_t>(EditorAuthoringView::Hud)]
              .workspace == nullptr,
      "Hidden HUD left a stale GPU render request");

  std::string error;
  require(shell.openDocument(project / "scenes/main.hud.json", error), error);
  for (int frame = 0; frame < 3; ++frame)
    imgui.frame(shell, "HUD");
  require(active("HUD") && active("Viewport") && active("UI Palette"),
          "Opening the HUD source did not reopen its independent panel");
  require(window("HUD").ID == hudId && window("Viewport").ID == sceneId,
          "Reopening HUD changed stable window identities");

  // Move the reopened HUD out of a possible tab group again so hiding the
  // scene cannot implicitly hide the HUD's content through tab selection.
  const ImGuiID dockspace = ImHashStr("DemiMainDockspace");
  const ImGuiID inspectorDock = window("Inspector").DockId;
  ImGui::DockBuilderDockWindow("HUD", inspectorDock);
  ImGui::DockBuilderFinish(dockspace);
  for (int frame = 0; frame < 3; ++frame)
    imgui.frame(shell, "HUD");
  closeDockTab(imgui, shell, "Viewport");
  for (int frame = 0; frame < 3; ++frame)
    imgui.frame(shell, "HUD");
  require(!active("Viewport") && active("HUD"),
          "Closing the Scene panel also closed HUD");
  require(shell.authoringViews()[static_cast<std::size_t>(
                                     EditorAuthoringView::Viewport)]
                  .workspace == nullptr,
          "Hidden Scene left a stale GPU render request");
  require(shell.openDocument(project / "scenes/main.scene.json", error), error);
  for (int frame = 0; frame < 3; ++frame)
    imgui.frame(shell, "Viewport");
  require(active("Viewport") && active("HUD") && !active("UI Palette"),
          "Reopening Scene did not restore scene focus and contextual palette");
  require(window("Viewport").ID == sceneId && window("HUD").ID == hudId,
          "Reopening Scene changed stable window identities");
}

void checkRuntimePanelsRejectDelete(ImGuiFixture &imgui, EditorShell &shell,
                                    EditorWorkspace &workspace,
                                    const fs::path &project) {
  workspace.selectEntity("player");
  const Json source = workspace.sceneDocument().json();
  const bool couldUndo = workspace.sceneDocument().canUndo();
  std::string error;
  require(
      shell.playSession().startEmbedded(project / "demi.project.json", error),
      error);
  require(shell.playSession().runtimeWorld() != nullptr &&
              shell.playSession().runtimeWorld() != &workspace.project().world,
          "Embedded Play did not create an isolated runtime world");

  for (const char *panel : {"Inspector", "Hierarchy"}) {
    // Game focus establishes the runtime inspection context. Moving to an
    // inspection panel must not fall back to destructive authored shortcuts.
    for (int frame = 0; frame < 3; ++frame)
      imgui.frame(shell, "Game View");
    require(shell.showingGameView(),
            "Game focus did not activate runtime panels");
    for (int frame = 0; frame < 3; ++frame)
      imgui.frame(shell, panel);
    require(active(panel) && shell.showingGameView() &&
                shell.playSession().isEmbedded(),
            std::string("Runtime panel context was not established: ") + panel);
    auto &input = ImGui::GetIO();
    input.AddKeyEvent(ImGuiKey_Delete, true);
    imgui.frame(shell, panel);
    input.AddKeyEvent(ImGuiKey_Delete, false);
    imgui.frame(shell, panel);
    require(
        workspace.sceneDocument().json() == source &&
            workspace.sceneDocument().canUndo() == couldUndo &&
            !workspace.sceneDocument().isDirty() &&
            demi::runtime::findEntity(workspace.project().world, "player") &&
            workspace.selectedEntityId() == "player",
        std::string("Delete from runtime ") + panel +
            " changed authored scene, history or selection");
    require(demi::runtime::findEntity(*shell.playSession().runtimeWorld(),
                                      "player"),
            std::string("Delete mutated the read-only runtime ") + panel);
    require(!active("UI Palette"),
            "Game inspection left the UI Palette active");
  }
  shell.playSession().stop();
  for (int frame = 0; frame < 3; ++frame)
    imgui.frame(shell, "Viewport");
  require(!shell.playSession().runtimeWorld() &&
              workspace.sceneDocument().json() == source,
          "Stopping embedded Play changed authored scene data");
}

} // namespace

int main() {
  try {
    checkFreshTerrainGraphStartupFocus();
    TestDirectory fixture;
    ScopedEnvironment data("XDG_DATA_HOME", fixture.root / "data");
    ScopedEnvironment cache("XDG_CACHE_HOME", fixture.root / "cache");
    require(defaultEditorDataDirectory() == fixture.root / "data/demi-editor" &&
                defaultEditorCacheDirectory() ==
                    fixture.root / "cache/demi-editor",
            "Shell preferences or recovery paths escaped the fixture");
    const fs::path project = fixture.root / "project";
    createProject(project);
    EditorWorkspace workspace;
    std::string error;
    require(workspace.open(project, error), error);
    const Json originalScene = workspace.sceneDocument().json();
    ImGuiFixture imgui;
    EditorShell shell(workspace);
    checkIndependentViews(imgui, shell, project);
    checkHudTextEditing(imgui, shell, project);
    checkHideAndReopen(imgui, shell, project);
    checkRuntimePanelsRejectDelete(imgui, shell, workspace, project);
    require(workspace.sceneDocument().json() == originalScene &&
                !workspace.hasUnsavedChanges(),
            "Docking or focusing views mutated authored game data");
    shell.releaseUiResources();
    std::cout << "Shell independent docking tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
