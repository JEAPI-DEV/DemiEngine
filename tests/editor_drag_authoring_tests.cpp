#include "editor/EditorAssetsPanel.h"
#include "editor/EditorDragDropPayloads.h"
#include "editor/EditorViewportPanel.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

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
    if (window->Active && std::string_view(window->Name).find(name) !=
                              std::string_view::npos)
      return window;
  return nullptr;
}

template <typename Draw>
void renderFrame(Draw &&draw) {
  ImGui::NewFrame();
  draw();
  ImGui::Render();
}

template <typename Draw>
void renderExternalDragFrame(const char *payloadType,
                             const std::string &payload, const ImVec2 mouse,
                             const bool mouseDown, Draw &&draw) {
  ImGuiIO &io = ImGui::GetIO();
  io.AddMousePosEvent(mouse.x, mouse.y);
  io.AddMouseButtonEvent(ImGuiMouseButton_Left, mouseDown);
  ImGui::NewFrame();
  require(ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceExtern |
                                     ImGuiDragDropFlags_SourceNoPreviewTooltip),
          "Could not begin external drag source");
  ImGui::SetDragDropPayload(payloadType, payload.c_str(), payload.size() + 1);
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
    demi::editor::drawEditorViewport(workspace, {0.0F, 0.0F},
                                     {900.0F, 700.0F}, UINT16_MAX, area,
                                     hudState, false, notice);
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

} // namespace

int main() {
  try {
    const auto unique =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
                          ("demi-editor-drag-authoring-" +
                           std::to_string(unique));
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

    EditorWorkspace workspace;
    std::string error;
    require(workspace.open(root, error), error);
    require(!workspace.sceneDocument().canUndo(),
            "Fresh fixture unexpectedly has undo history");

    initializeImGui();
    checkAssetsBackgroundDrop(workspace, root);
    checkViewportPrefabDrop(workspace, dropPrefab);
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
