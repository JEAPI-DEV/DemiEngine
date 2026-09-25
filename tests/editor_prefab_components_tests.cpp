#include "demi/runtime/scene/WorldQueries.h"
#include "editor/EditorInspectorPanel.h"
#include "editor/EditorLuaComponentMetadata.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const std::string &message) {
  if (!condition)
    throw std::runtime_error(message);
}

void write(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path);
  output << text;
  require(output.good(), "Could not write fixture");
}

bool hasComponent(const demi::editor::EditorWorkspace &workspace,
                  const std::string &id, const std::string &component) {
  const auto *entity = demi::runtime::findEntity(workspace.project().world, id);
  require(entity != nullptr, "Missing entity: " + id);
  return entity->serializedComponents.contains(component);
}

void checkInspector(demi::editor::EditorWorkspace &workspace) {
  ImGui::SetAllocatorFunctions(
      [](std::size_t size, void *) { return std::malloc(size); },
      [](void *memory, void *) { std::free(memory); });
  ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = {1200, 2400};
  io.DeltaTime = 1.0F / 60;
  io.Fonts->AddFontDefault();
  unsigned char *pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  demi::editor::EditorInspectorPanelState state;
  std::string notice;
  ImVec2 addPosition;
  ImVec2 menuPosition;
  for (int frame = 0; frame < 3; ++frame) {
    ImGui::NewFrame();
    ImGui::LogToBuffer();
    demi::editor::drawInspectorPanel(workspace, {0, 0}, {420, 2300}, state,
                                     notice);
    auto *window = ImGui::FindWindowByName("Inspector");
    require(window != nullptr, "Inspector missing");
    const std::string labels = ImGui::GetCurrentContext()->LogBuffer.c_str();
    require(labels.find("Add Component") != std::string::npos,
            "Prefab Add Component is not available");
    addPosition = {window->Pos.x + window->Size.x * 0.5F,
                   window->DC.CursorPosPrevLine.y +
                       ImGui::GetFrameHeight() * 0.5F};
    const ImGuiID componentId = ImHashStr("Transform3D", 0, window->ID);
    const ImGuiID tableId = ImHashStr("##component-header", 0, componentId);
    const auto *table = ImGui::GetCurrentContext()->Tables.GetByKey(tableId);
    require(table != nullptr, "Component header missing");
    menuPosition = {table->Columns[1].WorkMinX + 5,
                    table->OuterRect.Min.y + ImGui::GetTextLineHeight() * 0.5F +
                        2};
    ImGui::LogFinish();
    ImGui::Render();
  }
  io.AddMousePosEvent(addPosition.x, addPosition.y);
  for (int frame = 0; frame < 3; ++frame) {
    if (frame == 1)
      io.AddMouseButtonEvent(0, true);
    if (frame == 2)
      io.AddMouseButtonEvent(0, false);
    ImGui::NewFrame();
    demi::editor::drawInspectorPanel(workspace, {0, 0}, {420, 2300}, state,
                                     notice);
    ImGui::Render();
  }
  require(!ImGui::GetCurrentContext()->OpenPopupStack.empty(),
          "Add Component did not open");
  ImGui::ClosePopupToLevel(0, true);
  ImGui::ClearActiveID();
  io.AddMousePosEvent(menuPosition.x, menuPosition.y);
  for (int frame = 0; frame < 3; ++frame) {
    if (frame == 1)
      io.AddMouseButtonEvent(0, true);
    if (frame == 2)
      io.AddMouseButtonEvent(0, false);
    ImGui::NewFrame();
    demi::editor::drawInspectorPanel(workspace, {0, 0}, {420, 2300}, state,
                                     notice);
    ImGui::Render();
  }
  require(!ImGui::GetCurrentContext()->OpenPopupStack.empty(),
          "Component action menu did not open");
  ImGui::DestroyContext();
}
} // namespace

int main() {
  namespace fs = std::filesystem;
  using namespace demi::editor;
  using nlohmann::json;
  try {
    const auto unique =
        std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path root = fs::temp_directory_path() /
                          ("demi-prefab-components-" + std::to_string(unique));
    write(root / "demi.project.json", R"({
      "format_version":1,"name":"Prefab components",
      "main_scene":"scene://main","scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}]
    })");
    const std::string source = R"({
      "format_version":1,"id":"prefab://body",
      "entities":[{"id":"body","preset":"static_box_3d","components":{"Transform3D":{},"MeshRenderer":{"shape":"cube"}}}]
    })";
    write(root / "prefabs/body.prefab.json", source);
    write(root / "prefabs/wrapper.prefab.json", R"({
      "format_version":1,"id":"prefab://wrapper",
      "entities":[{"id":"inner","prefab":"prefab://body"}]
    })");
    write(root / "scenes/main.scene.json", R"({
      "format_version":1,"id":"scene://main","entities":[
        {"id":"a","prefab":"prefab://wrapper","overrides":{
          "inner/body.Rigidbody3D.mass":20,
          "inner/body.Transform3D.position":[1,2,3]}},
        {"id":"b","prefab":"prefab://wrapper"}]
    })");
    write(root / "scripts/behaviour.lua", "return {}\n");
    EditorWorkspace workspace;
    std::string error;
    require(workspace.open(root, error), error);
    const auto original = workspace.sceneDocument().json();
    require(workspace.addComponent("a/inner/body", "Dentable3D", error), error);
    require(hasComponent(workspace, "a/inner/body", "Dentable3D"),
            "Added component missing");
    require(!hasComponent(workspace, "b/inner/body", "Dentable3D"),
            "Sibling changed");
    require(!workspace.addComponent("a/inner/body", "Dentable3D", error),
            "Duplicate accepted");
    require(workspace.undo(error), error);
    require(workspace.sceneDocument().json() == original,
            "Undo did not restore exact source");
    require(workspace.redo(error), error);
    require(workspace.editValue({.entityId = "a/inner/body",
                                 .component = "Dentable3D",
                                 .field = "radius"},
                                0.3, false, error),
            error);
    require(workspace.undo(error), error);
    require(hasComponent(workspace, "a/inner/body", "Dentable3D"),
            "Property undo removed added component");
    require(workspace.redo(error), error);
    require(workspace.removeValue({.entityId = "a/inner/body",
                                   .component = "Dentable3D",
                                   .field = "radius"},
                                  error),
            error);
    require(hasComponent(workspace, "a/inner/body", "Dentable3D"),
            "Property reset removed added component");
    require(workspace.removeComponent("a/inner/body", "Dentable3D", error),
            error);
    require(!hasComponent(workspace, "a/inner/body", "Dentable3D"),
            "Removed component remains");
    require(workspace.removedComponentOverrides("a/inner/body").empty(),
            "Local addition left removal tombstone");
    require(workspace.undo(error), error);
    require(hasComponent(workspace, "a/inner/body", "Dentable3D"),
            "Undo removal failed");
    require(
        workspace.revertComponentOverride("a/inner/body", "Dentable3D", error),
        error);
    require(workspace.sceneDocument().json() == original,
            "Revert left local override behind");

    // Preset components can be removed locally without unpacking shared source.
    require(workspace.removeComponent("a/inner/body", "Rigidbody3D", error),
            error);
    require(workspace.removedComponentOverrides("a/inner/body") ==
                std::vector<std::string>{"Rigidbody3D"},
            "Inherited removal not available for restoration");
    require(
        !workspace.sceneDocument().json()["entities"][0]["overrides"].contains(
            "inner/body.Rigidbody3D.mass"),
        "Dotted field override survived component removal");
    require(workspace.undo(error), error);
    require(workspace.sceneDocument().json() == original,
            "Undo lost dotted override spelling");
    require(workspace.redo(error), error);
    require(!hasComponent(workspace, "a/inner/body", "Rigidbody3D"),
            "Preset resurrected removed body");
    require(hasComponent(workspace, "b/inner/body", "Rigidbody3D"),
            "Sibling lost its body");
    require(workspace.save(error), error);
    require(workspace.open(root, error), error);
    require(!hasComponent(workspace, "a/inner/body", "Rigidbody3D"),
            "Saved removal lost");
    require(
        workspace.revertComponentOverride("a/inner/body", "Rigidbody3D", error),
        error);
    require(hasComponent(workspace, "a/inner/body", "Rigidbody3D"),
            "Revert did not restore source");
    require(workspace.save(error), error);

    // This is the screenshot's case: editing a nested instance in a prefab tab.
    require(workspace.openPrefabDocument(root / "prefabs/wrapper.prefab.json",
                                         error),
            error);
    require(workspace.prefabSourcePath("inner/body") ==
                root / "prefabs/body.prefab.json",
            "Source navigation chose the wrapper instead of the nested source");
    workspace.selectEntity("inner/body");
    checkInspector(workspace);
    EditorLuaComponentMetadata script;
    script.module = "script://scripts/behaviour.lua";
    script.defaultProperties = json::object();
    require(workspace.addScriptComponent("inner/body", script, error), error);
    require(hasComponent(workspace, "inner/body", "LuaScript"),
            "Nested script missing");
    const auto beforeFailure = workspace.sceneDocument().json();
    require(!workspace.addComponent("inner/body", "UnknownComponent", error),
            "Unknown accepted");
    require(!workspace.addComponent("inner/body", "Dentable3D",
                                    {{"radius", -1}}, error),
            "Invalid values accepted");
    require(workspace.sceneDocument().json() == beforeFailure,
            "Failed edit changed document");
    require(workspace.save(error), error);
    require(workspace.open(root, error), error);
    require(hasComponent(workspace, "a/inner/body", "LuaScript"),
            "Saved wrapper addition missing");
    require(hasComponent(workspace, "b/inner/body", "LuaScript"),
            "Wrapper not shared");
    std::ifstream unchanged(root / "prefabs/body.prefab.json");
    const std::string contents{std::istreambuf_iterator<char>(unchanged), {}};
    require(contents == source,
            "Editing nested instance changed shared source");
    fs::remove_all(root);
    std::cout << "Prefab component authoring passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
