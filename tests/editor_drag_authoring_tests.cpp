#include "editor/EditorAssetsPanel.h"
#include "editor/EditorDragDropPayloads.h"
#include "editor/EditorHudNodeInspector.h"
#include "editor/EditorInspectorPanel.h"
#include "editor/EditorModuleCatalog.h"
#include "editor/EditorModulesPanel.h"
#include "editor/EditorViewportPanel.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using demi::editor::EditorAssetsPanel;
using demi::editor::EditorHudViewportState;
using demi::editor::EditorViewportArea;
using demi::editor::EditorWorkspace;
using nlohmann::json;

void require(const bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

void write(const fs::path &path, const std::string_view text) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path);
  output << text;
  require(output.good(), "Could not write fixture: " + path.string());
}

json readJson(const fs::path &path) {
  std::ifstream input(path);
  require(input.good(), "Could not read JSON fixture: " + path.string());
  return json::parse(input);
}

ImGuiWindow *activeWindowContaining(const std::string_view name) {
  ImGuiContext *context = ImGui::GetCurrentContext();
  for (ImGuiWindow *window : context->Windows)
    if (window->Active &&
        std::string_view(window->Name).find(name) != std::string_view::npos)
      return window;
  return nullptr;
}

template <typename Draw> void renderFrame(Draw &&draw) {
  ImGui::NewFrame();
  draw();
  ImGui::Render();
}

template <typename Draw>
void renderExternalDragFrame(const char *payloadType,
                             const std::string &payload, const ImVec2 mouse,
                             const bool mouseDown, Draw &&draw,
                             const bool terminated = true) {
  ImGuiIO &io = ImGui::GetIO();
  io.AddMousePosEvent(mouse.x, mouse.y);
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, mouseDown);
  ImGui::NewFrame();
  require(ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern |
                                     ImGuiDragDropFlags_SourceNoPreviewTooltip),
          "Could not begin external drag source");
  ImGui::SetDragDropPayload(payloadType, payload.c_str(),
                            payload.size() + (terminated ? 1 : 0));
  ImGui::EndDragDropSource();
  draw();
  ImGui::Render();
}

void initializeImGui() {
  ImGui::SetAllocatorFunctions(
      [](std::size_t size, void *) { return std::malloc(size); },
      [](void *memory, void *) { std::free(memory); });
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = {1400.0F, 1200.0F};
  io.DeltaTime = 1.0F / 60.0F;
  io.Fonts->AddFontDefault();
  unsigned char *pixels = nullptr;
  int width = 0;
  int height = 0;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  require(pixels != nullptr && width > 0 && height > 0,
          "Could not build the ImGui font atlas");
}

void checkInspectorTabs(EditorWorkspace &workspace,
                        const std::string_view stage) {
  demi::editor::EditorInspectorPanelState inspectorState;
  demi::editor::EditorHudInspectorState hudState;
  demi::editor::EditorModulesPanelState modulesState;
  std::string notice;
  std::vector<std::string> errors;
  ImGuiContext *context = ImGui::GetCurrentContext();
  context->ErrorCallback = [](ImGuiContext *, void *userData,
                              const char *message) {
    static_cast<std::vector<std::string> *>(userData)->emplace_back(message);
  };
  context->ErrorCallbackUserData = &errors;
  for (int frame = 0; frame < 3; ++frame) {
    ImGui::NewFrame();
    demi::editor::drawEditorInspector(workspace, {0.0F, 0.0F}, {420.0F, 700.0F},
                                      inspectorState, hudState, notice);
    ImGui::Render();
    require(ImGui::FindWindowByName("Inspector") != nullptr,
            "Inspector tab wrapper did not create its dock window");
    require(errors.empty() && context->ErrorCountCurrentFrame == 0,
            "Inspector tab stack error on " + std::string(stage) + ": " +
                (errors.empty() ? "missing ImGui End/Pop" : errors.front()));
  }
  context->ErrorCallback = nullptr;
  context->ErrorCallbackUserData = nullptr;
}

void checkAssetsBackgroundDrop(EditorWorkspace &workspace,
                               const fs::path &root) {
  EditorAssetsPanel panel;
  std::string notice;
  const auto drawAssets = [&] {
    panel.draw(workspace, {0.0F, 0.0F}, {1000.0F, 700.0F}, notice);
  };

  renderFrame(drawAssets);
  renderFrame(drawAssets);
  ImGuiWindow *grid = activeWindowContaining("asset-grid");
  require(grid != nullptr, "Assets grid child was not drawn");
  const ImVec2 gridBackground{grid->Pos.x + grid->Size.x * 0.5F,
                              grid->Pos.y + grid->Size.y - 20.0F};

  const json sceneBefore = workspace.sceneDocument().json();
  const std::string entityId = "player";
  renderExternalDragFrame(demi::editor::EditorSceneEntityPayload, entityId,
                          gridBackground, true, drawAssets);
  renderExternalDragFrame(demi::editor::EditorSceneEntityPayload, entityId,
                          gridBackground, false, drawAssets);

  ImGuiWindow *dialog = ImGui::FindWindowByName("New document");
  require(dialog != nullptr && dialog->Active,
          "Entity drop did not open the prefab naming dialog");
  // Let the newly opened dialog's name-field focus request settle before
  // activating another widget through keyboard navigation.
  renderFrame(drawAssets);
  ImGui::ActivateItemByID(dialog->GetID("Create prefab copy"));
  renderFrame(drawAssets);

  const fs::path created = root / "prefabs/Player_Prefab.prefab.json";
  require(fs::is_regular_file(created),
          "Create prefab copy did not write the expected source");
  const json prefab = readJson(created);
  require(prefab.at("id") == "prefab://Player_Prefab",
          "Created prefab has the wrong stable ID");
  require(prefab.at("entities") ==
              json::array({sceneBefore.at("entities").at(0)}),
          "Created prefab did not copy the dragged authored entity");
  require(workspace.sceneDocument().json() == sceneBefore,
          "Creating a prefab copy changed the authored scene");
  require(!workspace.sceneDocument().canUndo(),
          "Creating a prefab copy inserted a scene undo command");
}

void checkViewportPrefabDrop(EditorWorkspace &workspace,
                             const fs::path &prefabPath) {
  EditorViewportArea area;
  EditorHudViewportState hudState;
  std::string notice;
  const auto drawViewport = [&] {
    demi::editor::drawEditorViewport(workspace, {0.0F, 0.0F}, {900.0F, 700.0F},
                                     UINT16_MAX, area, hudState, false, notice);
  };

  renderFrame(drawViewport);
  renderFrame(drawViewport);
  require(area.width > 0 && area.height > 0,
          "Scene Viewport canvas was not drawn");
  // Stay clear of the selected entity's center gizmo while remaining over the
  // ground-facing canvas.
  const ImVec2 canvasDrop{static_cast<float>(area.x) + area.width * 0.72F,
                          static_cast<float>(area.y) + area.height * 0.68F};
  const json sceneBefore = workspace.sceneDocument().json();
  const std::string payload = prefabPath.string();
  renderExternalDragFrame(demi::editor::EditorPrefabSourcePayload, payload,
                          canvasDrop, true, drawViewport);
  renderExternalDragFrame(demi::editor::EditorPrefabSourcePayload, payload,
                          canvasDrop, false, drawViewport);

  require(notice == "Prefab instance placed",
          "Prefab source drop was not accepted by the Scene Viewport: " +
              notice);
  require(workspace.sceneDocument().canUndo(),
          "Viewport prefab drop did not insert an undo command");
  const json &sceneAfter = workspace.sceneDocument().json();
  require(sceneAfter.at("entities").size() ==
              sceneBefore.at("entities").size() + 1,
          "Viewport prefab drop inserted the wrong number of entities");
  const json &instance = sceneAfter.at("entities").back();
  require(instance.at("prefab") == "prefab://drop",
          "Viewport prefab drop used the wrong source");

  std::string error;
  require(workspace.undo(error), error);
  require(workspace.sceneDocument().json() == sceneBefore,
          "One undo did not remove the viewport prefab insertion");
  require(!workspace.sceneDocument().canUndo(),
          "Viewport prefab drop inserted more than one undo command");
}

void checkHudModuleDrop(EditorWorkspace &workspace, const fs::path &hudPath) {
  std::string error;
  require(workspace.openHudDocument(hudPath, error), error);
  demi::editor::EditorModulesPanelState modulesState;
  const auto drawModules = [&] {
    demi::editor::drawEditorPalettePanel(
        workspace, modulesState, demi::editor::EditorModuleKind::HudElement);
  };
  renderFrame(drawModules);
  const auto *cachedModules = modulesState.catalog.data();
  renderFrame(drawModules);
  require(modulesState.catalog.data() == cachedModules,
          "Modules panel rebuilt the catalog on an unchanged frame");
  const fs::path addedPrefab = hudPath.parent_path() / "new.ui.prefab.json";
  write(addedPrefab, R"({"format_version":1,"id":"ui-prefab://new",
    "root":{"id":"root","type":"panel","size":[120,40]}})");
  renderFrame(drawModules);
  require(demi::editor::resolveModule(modulesState.catalog,
                                      "prefab:ui-prefab://new") == nullptr,
          "Modules panel rescanned source files without a workspace refresh");
  const auto previousRevision = workspace.sourceIndexRevision();
  workspace.refreshAssetMetadata();
  require(workspace.sourceIndexRevision() > previousRevision,
          "Source refresh did not advance the catalog revision");
  renderFrame(drawModules);
  require(demi::editor::resolveModule(modulesState.catalog,
                                      "prefab:ui-prefab://new") != nullptr,
          "Modules panel did not discover a UI prefab after source refresh");
  require(workspace.openHudDocument(addedPrefab, error), error);
  require(workspace.createHudNode("label", error), error);
  const auto beforeSaveRevision = workspace.sourceIndexRevision();
  require(workspace.saveHud(error), error);
  require(workspace.sourceIndexRevision() > beforeSaveRevision,
          "Saving a UI prefab did not refresh source-derived catalog data");
  renderFrame(drawModules);
  const auto *updatedPrefab = demi::editor::resolveModule(
      modulesState.catalog, "prefab:ui-prefab://new");
  require(updatedPrefab != nullptr &&
              updatedPrefab->description.find("label") != std::string::npos,
          "Modules panel kept stale UI prefab content after save");
  require(workspace.openHudDocument(hudPath, error), error);

  const auto modules = demi::editor::editorModules(workspace);
  const auto *button = demi::editor::resolveModule(modules, "hud:button");
  require(button != nullptr &&
              button->kind == demi::editor::EditorModuleKind::HudElement &&
              button->value == "button",
          "Modules catalog did not provide the stable HUD button ID");

  EditorViewportArea area;
  EditorHudViewportState hudState;
  std::string notice;
  const auto drawHud = [&] {
    demi::editor::drawEditorViewport(workspace, {0.0F, 0.0F}, {900.0F, 700.0F},
                                     UINT16_MAX, area, hudState, true, notice);
  };
  renderFrame(drawHud);
  renderFrame(drawHud);
  require(area.width > 0 && area.height > 0, "HUD stage canvas was not drawn");
  const ImVec2 drop{static_cast<float>(area.x) + area.width * 0.62F,
                    static_cast<float>(area.y) + area.height * 0.57F};
  const json before = workspace.hudDocument()->json();
  renderExternalDragFrame(demi::editor::EditorModulePayload, button->id, drop,
                          true, drawHud);
  renderExternalDragFrame(demi::editor::EditorModulePayload, button->id, drop,
                          false, drawHud);

  require(notice == "Button added to HUD",
          "HUD stage did not accept the Modules payload: " + notice);
  const json &after = workspace.hudDocument()->json();
  require(after.at("root").at("children").size() == 1,
          "HUD drop did not create exactly one authored child");
  require(workspace.hudDocument()->isDirty(),
          "HUD drop did not mark its source document dirty");
  const json &created = after.at("root").at("children").front();
  require(created.at("id") == workspace.selectedHudNodeId() &&
              created.at("type") == "button",
          "HUD drop did not select the created button");
  const auto &canvas = workspace.displayedHud().canvasSize;
  const float expectedX = (drop.x - area.x) * canvas.x / area.width;
  const float expectedY = (drop.y - area.y) * canvas.y / area.height;
  require(std::abs(created.at("position")[0].get<float>() - expectedX) < 1.5F &&
              std::abs(created.at("position")[1].get<float>() - expectedY) <
                  1.5F,
          "HUD drop did not use the stage's authored coordinates");
  require(workspace.undo(error) && workspace.hudDocument()->json() == before,
          "One undo did not restore the HUD source after a module drop");
  require(!workspace.hudDocument()->canUndo(),
          "HUD module drop inserted more than one undo command");
  require(!workspace.hudDocument()->isDirty(),
          "Undo left the original HUD source marked dirty");

  renderExternalDragFrame(demi::editor::EditorModulePayload,
                          "hud:unknown_control", drop, true, drawHud);
  renderExternalDragFrame(demi::editor::EditorModulePayload,
                          "hud:unknown_control", drop, false, drawHud);
  require(notice == "This module is no longer available." &&
              workspace.hudDocument()->json() == before &&
              !workspace.hudDocument()->canUndo(),
          "Unknown module payload changed the HUD source");

  renderExternalDragFrame(demi::editor::EditorModulePayload, button->id, drop,
                          true, drawHud, false);
  renderExternalDragFrame(demi::editor::EditorModulePayload, button->id, drop,
                          false, drawHud, false);
  require(notice == "The module drag payload is invalid." &&
              workspace.hudDocument()->json() == before &&
              !workspace.hudDocument()->canUndo(),
          "Unterminated module payload changed the HUD source");
}

} // namespace

int main() {
  try {
    const auto unique =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root =
        fs::temp_directory_path() /
        ("demi-editor-drag-authoring-" + std::to_string(unique));
    write(root / "demi.project.json", R"({
      "format_version":1,
      "name":"Drag authoring",
      "main_scene":"scene://main",
      "scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}]
    })");
    write(root / "scenes/main.scene.json", R"({
      "format_version":1,
      "id":"scene://main",
      "entities":[{
        "id":"player",
        "name":"Player Prefab",
        "components":{"Transform3D":{"position":[0,0,0]}}
      }]
    })");
    const fs::path dropPrefab = root / "prefabs/drop.prefab.json";
    write(dropPrefab, R"({
      "format_version":1,
      "id":"prefab://drop",
      "entities":[{
        "id":"body",
        "components":{"Transform3D":{"position":[0,1,0]}}
      }]
    })");
    const fs::path hud = root / "ui/main.hud.json";
    write(hud, R"({
      "format_version":1,
      "canvas_size":[900,700],
      "root":{"id":"root","type":"container",
              "anchor_min":[0,0],"anchor_max":[1,1],"children":[]}
    })");

    EditorWorkspace workspace;
    std::string error;
    require(workspace.open(root, error), error);
    require(!workspace.sceneDocument().canUndo(),
            "Fresh fixture unexpectedly has undo history");

    initializeImGui();
    checkAssetsBackgroundDrop(workspace, root);
    checkViewportPrefabDrop(workspace, dropPrefab);
    workspace.selectEntity("player");
    checkInspectorTabs(workspace, "scene");
    checkHudModuleDrop(workspace, hud);
    workspace.selectHudNode("root");
    checkInspectorTabs(workspace, "HUD");
    ImGui::DestroyContext();

    fs::remove_all(root);
    std::cout << "Editor drag authoring passed\n";
    return 0;
  } catch (const std::exception &error) {
    if (ImGui::GetCurrentContext() != nullptr)
      ImGui::DestroyContext();
    std::cerr << error.what() << '\n';
    return 1;
  }
}
