#include "demi/runtime/terrain/TerrainGraph.h"
#include "demi/runtime/terrain/TerrainRecipe.h"
#include "editor/EditorClipboard.h"
#include "editor/EditorDragDropPayloads.h"
#include "editor/EditorKeyBindings.h"
#include "editor/EditorModulesPanel.h"
#include "editor/EditorShortcutInput.h"
#include "editor/EditorShortcutSettings.h"
#include "editor/EditorTerrainGraphDocument.h"
#include "editor/EditorTerrainGraphPanel.h"
#include "editor/EditorTerrainGraphSettings.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <imnodes.h>
#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using Json = nlohmann::json;
using demi::editor::EditorCommand;
using demi::editor::EditorTerrainGraphPanel;
using demi::editor::EditorWorkspace;

void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

void writeJson(const fs::path &path, const Json &value) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path);
  output << value.dump(2) << '\n';
  require(output.good(), "Could not write fixture: " + path.string());
}

struct TemporaryProject {
  fs::path root;
  TemporaryProject() {
    const std::string pattern =
        (fs::temp_directory_path() / "demi-terrain-graph-ui-XXXXXX").string();
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    const char *created = ::mkdtemp(buffer.data());
    require(created != nullptr, "Could not create terrain graph test project");
    root = created;
  }
  ~TemporaryProject() {
    if (root.empty())
      return;
    std::error_code error;
    fs::remove_all(root, error);
    if (error)
      std::cerr << "Could not clean terrain graph fixture: " << error.message()
                << '\n';
  }
  TemporaryProject(const TemporaryProject &) = delete;
  TemporaryProject &operator=(const TemporaryProject &) = delete;
};

void createProject(const fs::path &root, bool connectedGraph = true) {
  demi::runtime::TerrainRecipe recipe;
  recipe.size = {4, 4};
  recipe.cellsX = recipe.cellsZ = 4;
  recipe.chunkCells = 4;
  recipe.landforms.at("default").heightVariation = 0;
  if (connectedGraph)
    recipe.graph = demi::runtime::defaultTerrainGraph();
  const Json project = Json::parse(R"({
    "format_version": 1,
    "name": "Terrain graph UI",
    "main_scene": "scene://terrain_graph/main",
    "scenes": [{"id": "scene://terrain_graph/main",
                "path": "scenes/main.scene.json"}]
  })");
  Json scene = Json::parse(R"({
    "format_version": 1,
    "id": "scene://terrain_graph/main",
    "entities": [
      {"id": "terrain", "components": {
        "Transform3D": {}, "Terrain3D": {"recipe": {}}
      }},
      {"id": "other", "components": {"Transform3D": {}}}
    ]
  })");
  scene["entities"][0]["components"]["Terrain3D"]["recipe"] = recipe.toJson();
  writeJson(root / "demi.project.json", project);
  writeJson(root / "scenes/main.scene.json", scene);
}

void initializeImGui() {
  ImGui::SetAllocatorFunctions(
      [](std::size_t size, void *) { return std::malloc(size); },
      [](void *memory, void *) { std::free(memory); });
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = {1400.0F, 900.0F};
  io.DeltaTime = 1.0F / 60.0F;
  io.Fonts->AddFontDefault();
  unsigned char *pixels = nullptr;
  int width = 0;
  int height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  require(pixels != nullptr && width > 0 && height > 0,
          "Could not build the ImGui font atlas");
}

void renderFrame(EditorTerrainGraphPanel &panel, EditorWorkspace &workspace,
                 std::string &notice, float width = 1200.0F, float wheel = 0.0F,
                 ImVec2 mouse = {700.0F, 450.0F}) {
  ImGuiIO &io = ImGui::GetIO();
  io.AddMousePosEvent(mouse.x, mouse.y);
  if (wheel != 0.0F)
    io.AddMouseWheelEvent(0.0F, wheel);
  ImGui::NewFrame();
  ImGui::SetNextWindowPos({0.0F, 0.0F}, ImGuiCond_Always);
  ImGui::SetNextWindowSize({width, 760.0F}, ImGuiCond_Always);
  ImGui::Begin("Stage");
  panel.draw(workspace, notice);
  ImGui::End();
  ImGui::Render();
  require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame == 0,
          "Terrain graph frame reported an ImGui layout error");
  require(ImGui::GetDrawData() != nullptr,
          "Terrain graph frame produced no ImGui draw data");
}

void checkIntroCommentLayout(EditorWorkspace &workspace) {
  workspace.selectEntity("terrain");
  workspace.syncTerrainAuthoring();
  auto &draft = workspace.terrainAuthoring().draft();
  draft["graph"] = demi::runtime::builtinTerrainPresets().front().graph;
  EditorTerrainGraphPanel panel;
  panel.open(workspace);
  std::string notice, error;
  renderFrame(panel, workspace, notice);
  renderFrame(panel, workspace, notice);
  require(panel.executeCommand(workspace, EditorCommand::SelectAll, error),
          error);
  renderFrame(panel, workspace, notice);
  std::vector<int> ids(static_cast<std::size_t>(ImNodes::NumSelectedNodes()));
  ImNodes::GetSelectedNodes(ids.data());
  require(ids.size() == 6,
          "Intro graph did not expose its comments and generators");
  for (int id : ids)
    require(ImNodes::GetNodeDimensions(id).x < 380.0F,
            "Intro comment or edit control expanded across the graph canvas");
}

void checkShortcutInput() {
  using namespace demi::editor;
  EditorKeyBindings bindings;
  auto &input = ImGui::GetIO();
  input.AddKeyEvent(ImGuiMod_Ctrl, true);
  input.AddKeyEvent(ImGuiKey_D, true);
  ImGui::NewFrame();
  ImGui::Begin("Shortcut input test");
  require(editorShortcutPressed(bindings, EditorCommand::Duplicate,
                                EditorCommandContext::Scene),
          "Ctrl+D did not match authoring duplicate");
  require(!editorShortcutPressed(bindings, EditorCommand::Duplicate,
                                 EditorCommandContext::Game) &&
              editorShortcutPressed(bindings, EditorCommand::ReleaseGameInput,
                                    EditorCommandContext::Game),
          "Game cursor release conflicted with authoring duplicate");
  ImGui::End();
  ImGui::Render();
  input.AddKeyEvent(ImGuiKey_D, false);
  input.AddKeyEvent(ImGuiMod_Ctrl, false);
  ImGui::NewFrame();
  ImGui::Begin("Shortcut input test");
  ImGui::End();
  ImGui::Render();

  std::string error;
  const auto chord = parseEditorKeyChord("Ctrl+Shift+C", error);
  require(chord && bindings.assign(EditorCommand::Copy, {*chord}, error),
          error);
  input.AddKeyEvent(ImGuiMod_Ctrl, true);
  input.AddKeyEvent(ImGuiMod_Shift, true);
  input.AddKeyEvent(ImGuiKey_C, true);
  ImGui::NewFrame();
  ImGui::Begin("Shortcut input test");
  require(editorShortcutPressed(bindings, EditorCommand::Copy,
                                EditorCommandContext::TerrainGraph),
          "A custom shortcut did not match in the graph context");
  require(captureEditorKeyChord() == chord,
          "Shortcut recorder did not preserve key modifiers");
  ImGui::End();
  ImGui::Render();
  input.AddKeyEvent(ImGuiKey_C, false);
  input.AddKeyEvent(ImGuiMod_Ctrl, false);
  input.AddKeyEvent(ImGuiMod_Shift, false);
  ImGui::NewFrame();
  ImGui::Begin("Shortcut settings test");
  EditorShortcutSettingsState settings;
  drawEditorShortcutSettings(bindings, settings);
  ImGui::End();
  ImGui::Render();
  require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame == 0,
          "Shortcut settings produced an ImGui layout error");
}

void checkCanvasTransform() {
  demi::editor::EditorGraphCanvasView view;
  const ImVec2 anchor{450, 310};
  const ImVec2 pan{-100, 70};
  const auto point = view.toDocument({anchor.x - pan.x, anchor.y - pan.y});
  const auto next = view.wheelAt(3, anchor, pan);
  const auto restored = view.toDocument({anchor.x - next.x, anchor.y - next.y});
  require(view.zoom() > 1 && std::abs(point.x - restored.x) < 0.001F &&
              std::abs(point.y - restored.y) < 0.001F,
          "Zoom moved the document point under the cursor");
  const auto display = view.toDisplay({1234.5F, -760.25F});
  const auto document = view.toDocument(display);
  require(std::abs(document.x - 1234.5F) < 0.001F &&
              std::abs(document.y + 760.25F) < 0.001F,
          "Zoomed drag/drop coordinate conversion did not round-trip");
  (void)view.wheelAt(-3, anchor, next);
  require(std::abs(view.zoom() - 1) < 0.001F,
          "Opposite wheel input did not restore zoom");
}

ImGuiWindow *graphWindow(const char *part) {
  for (auto *window : ImGui::GetCurrentContext()->Windows)
    if (window->Active &&
        std::string_view(window->Name).find(part) != std::string_view::npos)
      return window;
  throw std::runtime_error(std::string("Missing graph window: ") + part);
}

void checkZoomAndOverlay(EditorWorkspace &workspace) {
  workspace.selectEntity("terrain");
  workspace.syncTerrainAuthoring();
  EditorTerrainGraphPanel panel;
  panel.open(workspace);
  std::string notice;
  const Json draftBefore = workspace.terrainAuthoring().draft();
  for (int frame = 0; frame < 3; ++frame)
    renderFrame(panel, workspace, notice);
  const float canvasWidth = graphWindow("Terrain graph canvas")->Size.x;
  const float expandedHeight =
      graphWindow("Terrain recipe settings overlay")->Size.y;
  const float before = panel.canvasZoom();
  renderFrame(panel, workspace, notice, 1200, 1);
  renderFrame(panel, workspace, notice);
  require(panel.canvasZoom() > before, "Wheel over canvas did not zoom in");
  renderFrame(panel, workspace, notice, 1200, -1);
  renderFrame(panel, workspace, notice);
  require(std::abs(panel.canvasZoom() - before) < 0.001F,
          "Wheel over canvas did not zoom out");
  require(workspace.terrainAuthoring().draft() == draftBefore,
          "Zoom changed authored node positions or parameters");
  for (int frame = 0; frame < 20; ++frame)
    renderFrame(panel, workspace, notice, 1200, 1);
  require(std::abs(panel.canvasZoom() - 2.5F) < 0.001F,
          "Zoom-in navigation bound was not applied");
  for (int frame = 0; frame < 30; ++frame)
    renderFrame(panel, workspace, notice, 1200, -1);
  require(std::abs(panel.canvasZoom() - 0.2F) < 0.001F,
          "Zoom-out navigation bound was not applied");
  renderFrame(panel, workspace, notice, 1200,
              std::log(before / panel.canvasZoom()) / 0.12F);
  renderFrame(panel, workspace, notice);
  require(workspace.terrainAuthoring().draft() == draftBefore,
          "Repeated zoom accumulated changes in authored coordinates");

  const auto *overlay = graphWindow("Terrain recipe settings overlay");
  const ImVec2 title{overlay->Pos.x + 30, overlay->Pos.y + 16};
  renderFrame(panel, workspace, notice, 1200, 0, title);
  ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
  renderFrame(panel, workspace, notice, 1200, 0, title);
  ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
  renderFrame(panel, workspace, notice, 1200, 0, title);
  renderFrame(panel, workspace, notice, 1200, 0, title);
  require(graphWindow("Terrain recipe settings overlay")->Size.y <
              expandedHeight / 2,
          "Collapsed recipe settings retained an empty full-height pane");
  require(graphWindow("Terrain graph canvas")->Size.x == canvasWidth,
          "Recipe settings reserved canvas width");
  const float canvasZoom = panel.canvasZoom();
  renderFrame(panel, workspace, notice, 1200, 1, title);
  renderFrame(panel, workspace, notice, 1200, 0, title);
  require(panel.canvasZoom() == canvasZoom,
          "Wheel over settings leaked through to graph zoom");
  panel.releaseUiResources();
}

void checkSettingsRendering(EditorWorkspace &workspace) {
  workspace.setViewDimension(
      demi::editor::EditorSceneViewDimension::ThreeDimensional);
  workspace.selectEntity("terrain");
  workspace.syncTerrainAuthoring();
  const Json before = workspace.terrainAuthoring().draft();
  const Json sourceBefore = workspace.sceneDocument().json();
  std::string notice;

  ImGui::NewFrame();
  ImGui::SetNextWindowPos({0.0F, 0.0F}, ImGuiCond_Always);
  ImGui::SetNextWindowSize({310.0F, 760.0F}, ImGuiCond_Always);
  ImGui::Begin("Narrow terrain settings");
  demi::editor::drawTerrainGraphSettings(workspace, notice);
  ImGui::End();
  ImGui::Render();

  require(workspace.terrainAuthoring().draft() == before,
          "Rendering narrow terrain settings changed the recipe draft");
  require(workspace.sceneDocument().json() == sourceBefore,
          "Rendering narrow terrain settings changed authored source");
  require(notice.empty(), "Rendering terrain settings produced an error");
}

void checkConventionalRecipe(EditorWorkspace &workspace) {
  workspace.setViewDimension(
      demi::editor::EditorSceneViewDimension::ThreeDimensional);
  workspace.selectEntity("terrain");
  workspace.syncTerrainAuthoring();
  const Json draftBefore = workspace.terrainAuthoring().draft();
  require(!draftBefore.contains("graph") || draftBefore["graph"].is_null(),
          "Conventional fixture unexpectedly contains a graph");
  const Json sourceBefore = workspace.sceneDocument().json();
  EditorTerrainGraphPanel panel;
  panel.open(workspace);
  std::string notice;
  renderFrame(panel, workspace, notice, 620.0F);
  renderFrame(panel, workspace, notice, 620.0F);
  require(workspace.terrainAuthoring().draft() == draftBefore,
          "Opening a conventional recipe created a graph implicitly");
  require(workspace.sceneDocument().json() == sourceBefore,
          "Showing conventional terrain changed the authored scene");
  panel.releaseUiResources();
}

void checkCardDragIntoGraph(EditorWorkspace &workspace) {
  using namespace demi::editor;
  workspace.selectEntity("terrain");
  workspace.syncTerrainAuthoring();
  EditorTerrainGraphPanel panel;
  panel.open(workspace);
  std::string notice;
  const Json before = workspace.terrainAuthoring().draft();
  const EditorModule module{"terrain:offset",
                            EditorModuleKind::TerrainNode,
                            "Modifiers",
                            "Height Offset",
                            "Raise or lower the connected terrain surface.",
                            "~",
                            "offset"};
  ImVec2 source;
  const auto frame = [&](ImVec2 mouse, bool down) {
    auto &io = ImGui::GetIO();
    io.AddMousePosEvent(mouse.x, mouse.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({950, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({400, 700}, ImGuiCond_Always);
    ImGui::Begin("Card palette gesture probe");
    const auto origin = ImGui::GetCursorScreenPos();
    source = {origin.x + 35, origin.y + 48};
    (void)drawEditorModuleCard(module);
    ImGui::End();
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({900, 760}, ImGuiCond_Always);
    ImGui::Begin("Card graph gesture probe");
    panel.draw(workspace, notice);
    ImGui::End();
    ImGui::Render();
    require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame == 0,
            "Card-to-graph gesture produced an ImGui warning");
  };
  frame({1400, 850}, false);
  frame(source, false);
  frame(source, true);
  frame({source.x + 20, source.y}, true);
  require(ImGui::GetDragDropPayload() &&
              ImGui::GetDragDropPayload()->IsDataType(EditorModulePayload),
          "Dragging the card description did not start a module drag");
  frame({700, 500}, true);
  require(workspace.terrainAuthoring().draft() == before,
          "Hovering a dragged card modified the graph before delivery");
  frame({700, 500}, false);
  frame({700, 500}, false);
  const auto &nodes =
      workspace.terrainAuthoring().draft().at("graph").at("nodes");
  require(nodes.size() == before.at("graph").at("nodes").size() + 1 &&
              nodes.back().at("type") == "offset",
          "Releasing a card over the graph did not create exactly one node: " +
              notice);
  require(panel.executeCommand(workspace, EditorCommand::Undo, notice), notice);
  require(workspace.terrainAuthoring().draft() == before,
          "Undo did not restore the graph before the card drop");
  panel.releaseUiResources();
}

void checkGraphCanvas(EditorWorkspace &workspace) {
  workspace.setViewDimension(
      demi::editor::EditorSceneViewDimension::ThreeDimensional);
  workspace.selectEntity("terrain");
  workspace.syncTerrainAuthoring();
  require(workspace.terrainAuthoring().entityId() == "terrain",
          "Terrain authoring did not bind the selected entity");
  const Json sourceBefore = workspace.sceneDocument().json();
  const Json draftBefore = workspace.terrainAuthoring().draft();
  require(draftBefore.at("graph") == demi::runtime::defaultTerrainGraph(),
          "Test terrain did not start with the connected graph");

  EditorTerrainGraphPanel panel;
  panel.open(workspace);
  std::string notice;
  for (int frame = 0; frame < 4; ++frame)
    renderFrame(panel, workspace, notice);
  require(panel.isOpen(), "Terrain graph panel closed during headless frames");
  require(ImNodes::GetCurrentContext() != nullptr,
          "Terrain graph did not create an imnodes context");

  require(panel.acceptModulePayload("terrain:offset"),
          "Terrain offset module was not accepted");
  renderFrame(panel, workspace, notice);
  const Json withOffset = workspace.terrainAuthoring().draft();
  require(withOffset.at("graph").at("nodes").size() ==
              draftBefore.at("graph").at("nodes").size() + 1,
          "Terrain module did not add one draft graph node");
  require(withOffset.at("graph").at("nodes").back().at("type") == "offset",
          "Terrain module added the wrong node type");
  require(workspace.sceneDocument().json() == sourceBefore,
          "Graph editing modified authored scene source before Generate");

  require(!panel.acceptModulePayload("terrain:missing"),
          "Unknown terrain module was accepted");
  require(!panel.acceptModulePayload("hud:button"),
          "A HUD module was accepted by the terrain graph");
  renderFrame(panel, workspace, notice);
  require(workspace.terrainAuthoring().draft() == withOffset,
          "Invalid module payload changed the terrain draft");

  require(panel.acceptModulePayload("terrain:water"),
          "Water module was not accepted");
  renderFrame(panel, workspace, notice);
  Json &draft = workspace.terrainAuthoring().draft();
  const Json waterNode = draft.at("graph").at("nodes").back();
  require(waterNode.at("type") == "water",
          "Water module added the wrong graph node");
  const std::string waterId = waterNode.at("id").get<std::string>();
  Json riverPath = waterNode.at("parameters").at("river_path");
  require(riverPath.is_array() && riverPath.size() == 2 &&
              riverPath[0].is_array() && riverPath[0].size() == 3,
          "River path is not a waypoint list of XYZ values");
  for (int index = 2; index < 20; ++index)
    riverPath.push_back({index * 2.0, -1.0, index * 2.0});
  demi::editor::EditorTerrainGraphDocument pathEdit;
  pathEdit.bind("ui#river-path");
  std::string error;
  require(pathEdit.setParameter(draft, waterId, "river_path", riverPath, error),
          error);
  renderFrame(panel, workspace, notice);
  renderFrame(panel, workspace, notice);
  require(
      draft.at("graph").at("nodes").back().at("parameters").at("river_path") ==
          riverPath,
      "Rendering the waypoint controls changed authored coordinates");
  require(pathEdit.undo(draft) && draft.at("graph")
                                          .at("nodes")
                                          .back()
                                          .at("parameters")
                                          .at("river_path")
                                          .size() == 2,
          "One graph draft undo did not restore the previous waypoint list");
  require(pathEdit.redo(draft) && draft.at("graph")
                                          .at("nodes")
                                          .back()
                                          .at("parameters")
                                          .at("river_path") == riverPath,
          "One graph draft redo did not restore the full waypoint list");
  renderFrame(panel, workspace, notice);
  const Json withWater = draft;

  workspace.selectEntity("other");
  workspace.syncTerrainAuthoring();
  renderFrame(panel, workspace, notice);
  require(workspace.terrainAuthoring().entityId().empty(),
          "Terrain graph stayed bound after selecting another entity");
  workspace.selectEntity("terrain");
  workspace.syncTerrainAuthoring();
  renderFrame(panel, workspace, notice);
  require(workspace.terrainAuthoring().draft() == withWater,
          "Returning to the terrain lost its unapplied graph draft");
  require(workspace.sceneDocument().json() == sourceBefore,
          "Selection change committed the graph draft unexpectedly");

  // The editor releases this context before the UI host destroys ImGui.
  panel.releaseUiResources();
  require(ImNodes::GetCurrentContext() == nullptr,
          "Terrain graph context survived explicit UI cleanup");
}

struct TestClipboard {
  std::string text;
  bool dropWrites = false;
  decltype(ImGuiPlatformIO::Platform_GetClipboardTextFn) previousGet;
  decltype(ImGuiPlatformIO::Platform_SetClipboardTextFn) previousSet;
  void *previousUserData;

  TestClipboard() {
    auto &platform = ImGui::GetPlatformIO();
    previousGet = platform.Platform_GetClipboardTextFn;
    previousSet = platform.Platform_SetClipboardTextFn;
    previousUserData = platform.Platform_ClipboardUserData;
    platform.Platform_ClipboardUserData = this;
    platform.Platform_GetClipboardTextFn = [](ImGuiContext *context) {
      const auto *clipboard = static_cast<const TestClipboard *>(
          context->PlatformIO.Platform_ClipboardUserData);
      return clipboard->text.c_str();
    };
    platform.Platform_SetClipboardTextFn = [](ImGuiContext *context,
                                              const char *text) {
      auto *clipboard = static_cast<TestClipboard *>(
          context->PlatformIO.Platform_ClipboardUserData);
      if (!clipboard->dropWrites)
        clipboard->text = text;
    };
  }

  ~TestClipboard() {
    auto &platform = ImGui::GetPlatformIO();
    platform.Platform_GetClipboardTextFn = previousGet;
    platform.Platform_SetClipboardTextFn = previousSet;
    platform.Platform_ClipboardUserData = previousUserData;
  }
};

void checkGraphClipboardCommands(EditorWorkspace &workspace) {
  workspace.selectEntity("terrain");
  workspace.syncTerrainAuthoring();
  EditorTerrainGraphPanel panel;
  demi::editor::EditorKeyBindings bindings;
  panel.setKeyBindings(bindings);
  panel.open(workspace);
  TestClipboard clipboard;
  std::string notice;
  std::string error;
  const Json original = workspace.terrainAuthoring().draft();
  const Json source = workspace.sceneDocument().json();
  renderFrame(panel, workspace, notice);
  renderFrame(panel, workspace, notice, 1200, -2);
  renderFrame(panel, workspace, notice);
  const float zoom = panel.canvasZoom();

  require(panel.executeCommand(workspace, EditorCommand::SelectAll, error),
          error);
  renderFrame(panel, workspace, notice);
  require(ImNodes::NumSelectedNodes() ==
              static_cast<int>(original.at("graph").at("nodes").size()),
          "Select All did not select every native graph node");
  require(panel.executeCommand(workspace, EditorCommand::Copy, error), error);
  const auto copied =
      demi::editor::decodeEditorClipboard(clipboard.text, error);
  require(copied &&
              copied->kind == demi::editor::EditorClipboardKind::TerrainGraph,
          "Graph copy did not publish the shared typed clipboard envelope");
  require(copied->data.at("links") == original.at("graph").at("links"),
          "Graph copy dropped the selected subgraph's internal links");
  require(workspace.terrainAuthoring().draft() == original,
          "Copy changed the graph draft");

  require(panel.executeCommand(workspace, EditorCommand::Paste, error), error);
  renderFrame(panel, workspace, notice);
  const Json pasted = workspace.terrainAuthoring().draft();
  require(pasted.at("graph").at("nodes").size() ==
              original.at("graph").at("nodes").size() * 2,
          "Paste did not insert all selected nodes");
  require(pasted.at("graph").at("links").size() ==
              original.at("graph").at("links").size() * 2,
          "Paste did not remap and retain internal links");
  require(ImNodes::NumSelectedNodes() ==
              static_cast<int>(original.at("graph").at("nodes").size()),
          "Paste did not select only the newly inserted nodes");
  require(panel.canvasZoom() == zoom,
          "A clipboard command changed view-only zoom");
  require(panel.executeCommand(workspace, EditorCommand::Undo, error), error);
  renderFrame(panel, workspace, notice);
  require(workspace.terrainAuthoring().draft() == original,
          "One graph Undo did not restore the complete paste");
  require(panel.executeCommand(workspace, EditorCommand::Redo, error), error);
  renderFrame(panel, workspace, notice);
  require(
      workspace.terrainAuthoring().draft() == pasted,
      "Graph Redo did not restore the same IDs, links and logical coordinates");
  require(panel.executeCommand(workspace, EditorCommand::Undo, error), error);
  renderFrame(panel, workspace, notice);

  require(panel.executeCommand(workspace, EditorCommand::SelectAll, error),
          error);
  clipboard.text = "previous clipboard text";
  clipboard.dropWrites = true;
  require(!panel.executeCommand(workspace, EditorCommand::Cut, error) &&
              !error.empty(),
          "Cut accepted a failed system clipboard write");
  require(workspace.terrainAuthoring().draft() == original,
          "A failed clipboard write deleted graph nodes");
  require(!panel.executeCommand(workspace, EditorCommand::Copy, error) &&
              !error.empty(),
          "Copy did not report a failed system clipboard write");
  clipboard.dropWrites = false;
  require(panel.executeCommand(workspace, EditorCommand::Cut, error), error);
  renderFrame(panel, workspace, notice);
  require(workspace.terrainAuthoring().draft().at("graph").at("nodes").empty(),
          "Cut did not remove all selected nodes");
  require(panel.executeCommand(workspace, EditorCommand::Undo, error), error);
  renderFrame(panel, workspace, notice);
  require(workspace.terrainAuthoring().draft() == original,
          "One graph Undo did not restore the cut subgraph");

  require(panel.executeCommand(workspace, EditorCommand::SelectAll, error),
          error);
  require(panel.executeCommand(workspace, EditorCommand::Duplicate, error),
          error);
  renderFrame(panel, workspace, notice);
  const Json duplicated = workspace.terrainAuthoring().draft();
  require(duplicated.at("graph").at("nodes").size() ==
              original.at("graph").at("nodes").size() * 2,
          "Duplicate ignored graph multiselection");
  require(panel.executeCommand(workspace, EditorCommand::Delete, error), error);
  renderFrame(panel, workspace, notice);
  require(workspace.terrainAuthoring().draft() == original,
          "Delete did not remove exactly the selected duplicated subgraph");
  require(panel.executeCommand(workspace, EditorCommand::Undo, error), error);
  renderFrame(panel, workspace, notice);
  require(workspace.terrainAuthoring().draft() == duplicated,
          "One Undo did not restore all deleted nodes and internal links");
  require(panel.executeCommand(workspace, EditorCommand::Undo, error), error);
  renderFrame(panel, workspace, notice);
  require(workspace.terrainAuthoring().draft() == original,
          "One Undo did not restore the complete multiselection duplicate");

  clipboard.text = "not editor content";
  require(!panel.executeCommand(workspace, EditorCommand::Paste, error),
          "Paste accepted malformed clipboard text");
  clipboard.text = demi::editor::encodeEditorClipboard(
      demi::editor::EditorClipboardKind::Entities, Json::array());
  require(!panel.executeCommand(workspace, EditorCommand::Paste, error),
          "The graph accepted a scene-entity clipboard payload");
  require(workspace.terrainAuthoring().draft() == original,
          "Rejected clipboard data changed the draft");
  require(panel.executeCommand(workspace, EditorCommand::FrameSelection, error),
          error);
  renderFrame(panel, workspace, notice);
  require(workspace.terrainAuthoring().draft() == original &&
              panel.canvasZoom() == zoom,
          "Framing the graph changed authored data or zoom");
  require(workspace.sceneDocument().json() == source,
          "Graph clipboard commands committed draft edits into source");
  // Also check the grouped icon toolbar in a narrow Stage.
  renderFrame(panel, workspace, notice, 390);
  panel.releaseUiResources();
}

void checkPinnedGraphCanvas(EditorWorkspace &workspace) {
  workspace.selectEntity("terrain");
  std::string error;
  require(workspace.pinTerrainAuthoring("terrain", error), error);
  EditorTerrainGraphPanel panel;
  panel.open(workspace);
  std::string notice;
  const Json draft = workspace.terrainAuthoring().draft();
  const Json scene = workspace.sceneDocument().json();
  for (int frame = 0; frame < 3; ++frame)
    renderFrame(panel, workspace, notice);
  workspace.terrainAuthoring().brush.mode =
      demi::editor::EditorTerrainBrush::Raise;
  workspace.selectEntity("other");
  for (int frame = 0; frame < 3; ++frame)
    renderFrame(panel, workspace, notice);
  require(graphWindow("Terrain graph canvas")->Active && panel.isOpen() &&
              workspace.terrainAuthoring().entityId() == "terrain" &&
              workspace.selectedEntityId() == "other" &&
              !workspace.terrainAuthoring().brushActive(),
          "Drawing the pinned graph changed Inspector selection or brush "
          "ownership");
  require(panel.executeCommand(workspace, EditorCommand::SelectAll, error),
          error);
  renderFrame(panel, workspace, notice);
  require(
      ImNodes::NumSelectedNodes() ==
          static_cast<int>(draft.at("graph").at("nodes").size()),
      "Pinned graph commands stopped working after selecting another entity");
  require(workspace.terrainAuthoring().draft() == draft &&
              workspace.sceneDocument().json() == scene,
          "Pinned graph presentation mutated its source or draft");
  require(workspace.unpinTerrainAuthoring(error), error);
  require(workspace.terrainAuthoring().entityId().empty() &&
              !workspace.terrainAuthoring().brushActive(),
          "Unpin did not restore selection-based graph/brush binding");
  panel.releaseUiResources();
}

} // namespace

int main() {
  try {
    TemporaryProject project;
    createProject(project.root);
    EditorWorkspace workspace;
    std::string error;
    require(workspace.open(project.root, error), error);
    initializeImGui();
    checkShortcutInput();
    checkCanvasTransform();
    TemporaryProject dragProject;
    createProject(dragProject.root);
    EditorWorkspace dragWorkspace;
    require(dragWorkspace.open(dragProject.root, error), error);
    checkCardDragIntoGraph(dragWorkspace);
    checkGraphCanvas(workspace);
    checkZoomAndOverlay(workspace);
    checkSettingsRendering(workspace);
    TemporaryProject clipboardProject;
    createProject(clipboardProject.root);
    EditorWorkspace clipboardWorkspace;
    require(clipboardWorkspace.open(clipboardProject.root, error), error);
    checkGraphClipboardCommands(clipboardWorkspace);
    TemporaryProject conventional;
    createProject(conventional.root, false);
    EditorWorkspace conventionalWorkspace;
    require(conventionalWorkspace.open(conventional.root, error), error);
    checkConventionalRecipe(conventionalWorkspace);
    TemporaryProject pinned;
    createProject(pinned.root);
    EditorWorkspace pinnedWorkspace;
    require(pinnedWorkspace.open(pinned.root, error), error);
    checkPinnedGraphCanvas(pinnedWorkspace);
    checkIntroCommentLayout(pinnedWorkspace);
    ImGui::DestroyContext();
    std::cout << "Editor terrain graph UI passed\n";
    return 0;
  } catch (const std::exception &error) {
    if (ImGui::GetCurrentContext() != nullptr)
      ImGui::DestroyContext();
    std::cerr << error.what() << '\n';
    return 1;
  }
}
