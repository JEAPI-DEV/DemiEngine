#include "demi/runtime/scene/WorldQueries.h"
#include "editor/EditorInspectorPanel.h"
#include "editor/EditorHierarchyPanel.h"
#include "editor/EditorLuaComponentMetadata.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <chrono>
#include <cmath>
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

void checkHierarchyReferenceDrop(const std::filesystem::path &root) {
  using namespace demi::editor;
  write(root / "demi.project.json", R"({
    "format_version":1,"name":"Reference drop","main_scene":"scene://main",
    "scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}]
  })");
  write(root / "scenes/main.scene.json", R"({
    "format_version":1,"id":"scene://main","entities":[
      {"id":"sensor","name":"Sensor","components":{
        "Transform3D":{},"LuaScript":{"module":"script://scripts/sensor.lua"}}},
      {"id":"cube","name":"Cube","preset":"static_box_3d"}
    ]
  })");
  write(root / "scripts/sensor.lua", R"(---@demi_component
local Sensor = {}
---@demi_property entity
Sensor.target = ""
return Sensor
)");
  EditorWorkspace workspace;
  std::string error;
  require(workspace.open(root, error), error);
  workspace.selectEntity("sensor");

  ImGui::SetAllocatorFunctions(
      [](std::size_t size, void *) { return std::malloc(size); },
      [](void *memory, void *) { std::free(memory); });
  ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = {1200, 900};
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.DeltaTime = 1.0F / 60;
  io.Fonts->AddFontDefault();
  unsigned char *pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  EditorHierarchyPanel hierarchy;
  EditorInspectorPanelState inspector;
  std::string notice;
  ImVec2 source;
  ImVec2 target;
  const auto draw = [&] {
    ImGui::NewFrame();
    hierarchy.draw(workspace, {0, 0}, {300, 900}, false, notice);
    // Cube is the last row. End() restores the parent window's LastItemData,
    // so use the hierarchy's retained content bounds rather than that item.
    auto *hierarchyWindow = ImGui::FindWindowByName("Hierarchy");
    require(hierarchyWindow != nullptr, "Missing Hierarchy");
    source = {hierarchyWindow->WorkRect.Min.x + 70,
              hierarchyWindow->DC.CursorMaxPos.y - ImGui::GetTextLineHeight() / 2};
    drawInspectorPanel(workspace, {700, 0}, {500, 900}, inspector, notice);
    auto *window = ImGui::FindWindowByName("Inspector");
    require(window != nullptr, "Missing Inspector");
    const auto scriptId = ImHashStr("LuaScript", 0, window->ID);
    const auto fieldId = ImHashStr("target", 0, scriptId);
    const auto tableId = ImHashStr("##script-properties", 0, fieldId);
    const auto *table = ImGui::GetCurrentContext()->Tables.GetByKey(tableId);
    require(table != nullptr, "Missing target property table");
    target = {table->Columns[1].WorkMinX + 30,
              table->OuterRect.Min.y + ImGui::GetFrameHeight() / 2};
    ImGui::Render();
  };
  draw();
  draw();
  io.AddMousePosEvent(source.x, source.y);
  draw();
  io.AddMouseButtonEvent(0, true);
  draw();
  require(workspace.selectedEntityId() == "sensor",
          "Drag press changed inspected selection");
  io.AddMousePosEvent(source.x + 30, source.y);
  draw();
  require(ImGui::GetDragDropPayload() != nullptr,
          "Hierarchy did not start a real drag");
  io.AddMousePosEvent(target.x, target.y);
  draw();
  draw();
  io.AddMouseButtonEvent(0, false);
  draw();
  require(workspace.selectedEntityId() == "sensor",
          "Drop changed inspected selection");
  const auto *component = workspace.sceneDocument().component("sensor", "LuaScript");
  require(component->at("properties").at("target") == "cube",
          "Real hierarchy drop did not assign cube: " + notice);
  require(workspace.undo(error), error);
  component = workspace.sceneDocument().component("sensor", "LuaScript");
  require(!component->contains("properties"), "Undo retained assignment");
  require(workspace.redo(error), error);
  require(workspace.save(error), error);
  require(workspace.open(root, error), error);
  component = workspace.sceneDocument().component("sensor", "LuaScript");
  require(component->at("properties").at("target") == "cube",
          "Reload lost assigned reference");

  const auto renameFrame = [&] {
    ImGui::NewFrame();
    hierarchy.draw(workspace, {0, 0}, {300, 900}, false, notice);
    ImGui::Render();
  };
  renameFrame();
  io.AddMousePosEvent(source.x, source.y);
  renameFrame();
  io.AddMouseButtonEvent(0, true);
  renameFrame();
  io.AddMouseButtonEvent(0, false);
  renameFrame();
  require(workspace.selectedEntityId() == "cube",
          "Hierarchy click did not select rename target");

  const auto rename = [&](const char *name, bool clickButton) {
    hierarchy.requestRename(std::string(workspace.selectedEntityId()));
    renameFrame();
    renameFrame();
    require(!ImGui::GetCurrentContext()->OpenPopupStack.empty(),
            "Rename dialog did not open");
    auto *popup = ImGui::GetCurrentContext()->OpenPopupStack.back().Window;
    require(popup != nullptr, "Missing rename popup window");
    io.AddMousePosEvent(popup->WorkRect.Min.x + 40,
                        popup->WorkRect.Min.y + ImGui::GetFrameHeight() / 2);
    renameFrame();
    io.AddMouseButtonEvent(0, true);
    renameFrame();
    io.AddMouseButtonEvent(0, false);
    renameFrame();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    renameFrame();
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    renameFrame();
    io.AddInputCharactersUTF8(name);
    for (int frame = 0; frame < 3; ++frame)
      renameFrame();
    if (clickButton) {
      const ImVec2 button{popup->WorkRect.Min.x + 15,
                          popup->DC.CursorMaxPos.y - ImGui::GetFrameHeight() / 2};
      io.AddMousePosEvent(button.x, button.y);
      renameFrame();
      io.AddMouseButtonEvent(0, true);
      renameFrame();
      io.AddMouseButtonEvent(0, false);
      renameFrame();
    } else {
      io.AddKeyEvent(ImGuiKey_Enter, true);
      renameFrame();
      io.AddKeyEvent(ImGuiKey_Enter, false);
      renameFrame();
    }
    require(ImGui::GetCurrentContext()->OpenPopupStack.empty(),
            "Rename dialog did not submit");
    require(workspace.sceneDocument().entity("cube")->at("name") == name,
            "Rename did not apply the entered name: " + notice + " / " +
                workspace.sceneDocument().entity("cube")->at("name").get<std::string>());
    renameFrame();
  };
  rename("Renamed by mouse", true);
  require(workspace.undo(error), error);
  require(workspace.sceneDocument().entity("cube")->at("name") == "Cube",
          "Rename Undo did not restore previous name");
  require(workspace.redo(error), error);
  rename("Renamed by Enter", false);
  require(workspace.save(error), error);
  require(workspace.open(root, error), error);
  require(workspace.sceneDocument().entity("cube")->at("name") == "Renamed by Enter",
          "Reload lost renamed name");
  require(workspace.sceneDocument().component("sensor", "LuaScript")
              ->at("properties").at("target") == "cube",
          "Rename changed the stable reference ID");
  ImGui::DestroyContext();
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
  // Activate each actual axis input and compare the text caret baselines.
  // This catches first-column baseline drift in nested property tables.
  auto *inspector = ImGui::FindWindowByName("Inspector");
  const ImGuiID component = ImHashStr("Transform3D", 0, inspector->ID);
  const ImGuiID field = ImHashStr("position", 0, component);
  const ImGuiID properties = ImHashStr("##component-properties", 0, field);
  const ImGuiID axes = ImHashStr("##vector-axes", 0, properties);
  float baseline = 0;
  for (int axis = 0; axis < 3; ++axis) {
    const ImGuiID axisSeed = ImHashData(&axis, sizeof(axis), axes);
    const ImGuiID input = ImHashStr("##axis", 0, axisSeed);
    ImGui::ActivateItemByID(input);
    ImGui::NewFrame();
    demi::editor::drawInspectorPanel(workspace, {0, 0}, {420, 2300}, state, notice);
    ImGui::Render();
    auto *context = ImGui::GetCurrentContext();
    require(context->InputTextState.ID == input, "Axis input not reached");
    const float currentBaseline = context->PlatformImeData.InputPos.y;
    if (axis == 0)
      baseline = currentBaseline;
    else
      require(std::abs(currentBaseline - baseline) < 0.1F, "Vector inputs are misaligned");
  }
  ImGui::ClearActiveID();
  ImGui::SetScrollY(inspector, inspector->ScrollMax.y);
  for (int frame = 0; frame < 2; ++frame) {
    ImGui::NewFrame();
    demi::editor::drawInspectorPanel(workspace, {0, 0}, {420, 2300}, state,
                                     notice);
    ImGui::Render();
  }
  ImGui::ActivateItemByID(ImGui::FindWindowByName("Inspector")->GetID("##add-component"));
  for (int frame = 0; frame < 3; ++frame) {
    ImGui::NewFrame();
    demi::editor::drawInspectorPanel(workspace, {0, 0}, {420, 2300}, state,
                                     notice);
    ImGui::Render();
  }
  require(!ImGui::GetCurrentContext()->OpenPopupStack.empty(),
          "Add Component did not open");
  ImGui::ClosePopupToLevel(0, true);
  ImGui::ClearActiveID();
  ImGui::SetScrollY(inspector, 0);
  for (int frame = 0; frame < 2; ++frame) {
    ImGui::NewFrame();
    demi::editor::drawInspectorPanel(workspace, {0, 0}, {420, 2300}, state,
                                     notice);
    ImGui::Render();
  }
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
    checkHierarchyReferenceDrop(root / "reference-drop");
    write(root / "demi.project.json", R"({
      "format_version":1,"name":"Prefab components",
      "main_scene":"scene://main","scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}]
    })");
    const std::string source = R"({
      "format_version":1,"id":"prefab://body",
      "entities":[{"id":"body","preset":"static_box_3d","components":{"Transform3D":{},"MeshRenderer":{"shape":"cube"},
        "GameplayData":{"values":{"health":100,"obsolete":true,"nested":{"old":1}}}}}]
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
        {"id":"b","prefab":"prefab://wrapper"},
        {"id":"kept","prefab":"prefab://body","entity_ids":{"body":"kept"}}]
    })");
    write(root / "scripts/behaviour.lua", "return {}\n");
    EditorWorkspace workspace;
    std::string error;
    require(workspace.open(root, error), error);
    workspace.selectEntity("kept");
    checkInspector(workspace);
    require(workspace.editValue({.entityId = "kept",
                                 .component = "Transform3D",
                                 .field = "position"},
                                json::array({4, 5, 6}), false, error),
            error);
    require(workspace.sceneDocument()
                    .json()["entities"][2]
                    .at("overrides")
                    .at("body")
                    .at("components")
                    .at("Transform3D")
                    .at("position") == json::array({4, 5, 6}),
            "Mapped prefab root edit did not stay on its instance");
    require(workspace.undo(error), error);
    const SceneValueTarget dataTarget{.entityId="a/inner/body", .component="GameplayData", .field="values"};
    const json editedData{{"health", 80}, {"nested", {{"new", nullptr}}}, {"items", json::array({1, true, "item"})}};
    const auto beforeData = workspace.sceneDocument().json();
    require(workspace.editValue(dataTarget, editedData, false, error), error);
    require(workspace.save(error), error);
    require(workspace.open(root, error), error);
    const auto *loadedData = demi::runtime::findEntity(workspace.project().world, "a/inner/body");
    require(json::parse(loadedData->serializedComponents.at("GameplayData")).at("values") == editedData,
            "Prefab object replacement merged deleted keys or lost null data");
    require(workspace.removeValue(dataTarget, error), error);
    require(workspace.sceneDocument().json() == beforeData, "Reset object override did not restore source shape");
    require(workspace.undo(error), error);
    require(workspace.redo(error), error);
    require(workspace.save(error), error);
    const auto linkedScene = workspace.sceneDocument().json();
    require(
        workspace.reparentEntity("a/inner/body", std::string("kept"), error),
        error);
    require(
        !workspace.reparentEntity("kept", std::string("a/inner/body"), error),
        "Cross-prefab parent cycle accepted");
    const auto movedScene = workspace.sceneDocument().json();
    require(workspace.unpackPrefab("a/inner/body", error), error);
    require(workspace.sceneDocument().entity("a/inner/body") != nullptr,
            "Unpack lost the stable child ID");
    require(workspace.sceneDocument()
                    .component("a/inner/body", "Transform3D")
                    ->at("parent") == "kept",
            "Unpack lost the overridden external parent");
    require(hasComponent(workspace, "a/inner/body", "Rigidbody3D"),
            "Unpack lost inherited components");
    require(workspace.undo(error), error);
    require(workspace.sceneDocument().json() == movedScene,
            "Unpack undo changed source");
    require(workspace.undo(error), error);
    require(workspace.sceneDocument().json() == linkedScene,
            "Reparent undo changed source");
    require(workspace.unpackPrefab("kept", error), error);
    require(workspace.sceneDocument().entity("kept") != nullptr &&
                !workspace.sceneDocument().entity("kept")->contains("prefab"),
            "Mapped-ID instance was not unpacked");
    require(workspace.undo(error), error);
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
