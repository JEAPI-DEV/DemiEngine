#include "editor/EditorHierarchyPanel.h"
#include "editor/EditorModuleCatalog.h"
#include "editor/EditorWorkspace.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <stdexcept>

namespace {
using namespace demi::editor;
using Json = nlohmann::json;
namespace fs = std::filesystem;
void require(bool value, const std::string &message) {
  if (!value)
    throw std::runtime_error(message);
}
void write(const fs::path &path, const Json &value) {
  fs::create_directories(path.parent_path());
  std::ofstream out(path);
  out << value.dump(2);
  require(out.good(), "Could not write fixture");
}
ImGuiWindow &window() {
  auto *value = ImGui::FindWindowByName("Hierarchy");
  require(value, "Missing Hierarchy");
  return *value;
}
ImGuiID treeId(std::initializer_list<const char *> path) {
  ImGuiID result = window().ID;
  for (const auto *part : path)
    result = ImHashStr(part, 0, result);
  return result;
}
void close(std::initializer_list<const char *> path) {
  window().StateStorage.SetInt(treeId(path), 0);
}
bool opened(std::initializer_list<const char *> path) {
  return window().StateStorage.GetInt(treeId(path), 0) != 0;
}
void frame(EditorHierarchyPanel &panel, EditorWorkspace &workspace, bool hud) {
  ImGui::NewFrame();
  std::string notice;
  panel.draw(workspace, {0, 0}, {340, 280}, hud, notice);
  ImGui::Render();
  require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame == 0,
          "Hierarchy ImGui error");
}
void check(const fs::path &root) {
  write(root / "demi.project.json",
        Json::parse(R"({"format_version":1,"name":"Reveal",
    "main_scene":"scene://main","scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}]})"));
  write(root / "prefabs/branch.prefab.json",
        Json::parse(R"({"format_version":1,"id":"prefab://branch","entities":[
    {"id":"body","components":{"Transform3D":{}},"children":[{"id":"leaf","components":{"Transform3D":{}}}]}]})"));
  Json scene =
      Json::parse(R"({"format_version":1,"id":"scene://main","entities":[
    {"id":"group","components":{"Transform3D":{}},"children":[
      {"id":"middle","components":{"Transform3D":{}},"children":[{"id":"leaf","components":{"Transform3D":{}}}]},
      {"id":"branch","prefab":"prefab://branch"}]},
    {"id":"unrelated","components":{"Transform3D":{}},"children":[{"id":"other","components":{"Transform3D":{}}}]}]})");
  write(root / "scenes/main.scene.json", scene);
  Json hud = Json::parse(
      R"({"format_version":1,"canvas_size":[640,360],"root":{"id":"root","type":"container","children":[
    {"id":"panel","type":"panel","size":[600,300],"stack":"column","children":[]}]}})");
  for (int i = 0; i < 60; ++i)
    hud["root"]["children"][0]["children"].push_back(
        {{"id", "label_" + std::to_string(i)},
         {"type", "label"},
         {"text", "Item"},
         {"size", {80, 20}}});
  hud["root"]["children"][0]["children"].push_back({{"id", "leaf"},
                                                    {"type", "label"},
                                                    {"text", "Last"},
                                                    {"size", {80, 20}}});
  write(root / "hud/main.hud.json", hud);
  EditorWorkspace workspace;
  std::string error;
  require(workspace.open(root, error), error);
  EditorHierarchyPanel panel;
  frame(panel, workspace, false);
  frame(panel, workspace, false);
  close({"Scene", "group"});
  close({"Scene", "group", "middle"});
  close({"Scene", "unrelated"});
  close({"Scene"});
  workspace.selectEntity("leaf");
  frame(panel, workspace, false);
  frame(panel, workspace, false);
  require(opened({"Scene"}) && opened({"Scene", "group"}) &&
              opened({"Scene", "group", "middle"}),
          "Scene selection did not reveal ancestors");
  require(!opened({"Scene", "unrelated"}),
          "Reveal expanded an unrelated branch");
  close({"Scene", "group"});
  frame(panel, workspace, false);
  require(!opened({"Scene", "group"}),
          "Unchanged selection forced a manual collapse open");
  workspace.selectEntity("leaf");
  frame(panel, workspace, false);
  require(opened({"Scene", "group"}),
          "Re-selecting the same entity did not reveal it");
  close({"Scene", "group"});
  close({"Scene", "group", "branch/body"});
  workspace.selectEntity("branch/leaf");
  frame(panel, workspace, false);
  require(opened({"Scene", "group"}) &&
              opened({"Scene", "group", "branch/body"}),
          "Expanded prefab selection was not revealed");
  workspace.selectEntity("leaf");
  close({"Scene", "unrelated"});
  require(workspace.reparentEntity("leaf", "unrelated", error), error);
  frame(panel, workspace, false);
  require(opened({"Scene", "unrelated"}),
          "Reparented selection was not revealed");
  require(workspace.undo(error), error);
  require(workspace.sceneDocument().json() == scene &&
              !workspace.sceneDocument().canUndo(),
          "Hierarchy reveal mutated the scene");

  require(workspace.openHudDocument(root / "hud/main.hud.json", error), error);
  frame(panel, workspace, true);
  frame(panel, workspace, true);
  close({"##hud-root", "##hud-node-root"});
  close({"##hud-root"});
  workspace.selectHudNode("leaf");
  frame(panel, workspace, true);
  frame(panel, workspace, true);
  require(opened({"##hud-root"}) && opened({"##hud-root", "##hud-node-root"}) &&
              opened({"##hud-root", "##hud-node-root", "##hud-node-panel"}),
          "HUD selection did not reveal ancestors");
  require(window().Scroll.y > 0,
          "Selected HUD control was not scrolled into view");
  close({"##hud-root", "##hud-node-root"});
  frame(panel, workspace, true);
  require(!opened({"##hud-root", "##hud-node-root"}),
          "HUD branch was forced open every frame");
  workspace.selectHudNode("leaf");
  frame(panel, workspace, true);
  require(opened({"##hud-root", "##hud-node-root"}),
          "Re-selecting the same HUD control did not reveal it");
  ImGui::ActivateItemByID(window().GetID("##hierarchy-search"));
  frame(panel, workspace, true);
  ImGui::GetIO().AddInputCharactersUTF8("not-found");
  frame(panel, workspace, true);
  frame(panel, workspace, true);
  require(window().ContentSize.y < 400, "Search did not hide the HUD rows");
  ImGui::ClearActiveID();
  workspace.selectHudNode("leaf");
  frame(panel, workspace, true);
  frame(panel, workspace, true);
  require(window().ContentSize.y > 600,
          "Selection remained hidden by the search filter");
  require(workspace.hudDocument()->json() == hud &&
              !workspace.hudDocument()->canUndo(),
          "HUD reveal changed authored data");
  close({"##hud-root", "##hud-node-root"});
  const auto modules = editorModules(workspace);
  const auto *label = resolveModule(modules, "hud:label");
  require(label, "Missing label module");
  require(workspace.placeHudModule(*label, {20, 20}, "panel", error), error);
  const auto added = std::string(workspace.selectedHudNodeId());
  frame(panel, workspace, true);
  frame(panel, workspace, true);
  require(workspace.isHudNodeSelected(added) &&
              opened({"##hud-root", "##hud-node-root"}) &&
              opened({"##hud-root", "##hud-node-root", "##hud-node-panel"}),
          "Palette drop did not reveal the selected control");
  require(workspace.undo(error), error);
  require(workspace.hudDocument()->json() == hud,
          "Reveal added an authored Undo command");
}
} // namespace
int main() {
  auto pattern =
      (fs::temp_directory_path() / "demi-hierarchy-reveal-XXXXXX").string();
  std::vector<char> bytes(pattern.begin(), pattern.end());
  bytes.push_back('\0');
  const auto *created = mkdtemp(bytes.data());
  if (!created)
    return 1;
  const fs::path root = created;
  ImGui::SetAllocatorFunctions(
      [](std::size_t size, void *) { return std::malloc(size); },
      [](void *p, void *) { std::free(p); });
  ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = {1000, 800};
  io.DeltaTime = 1.0F / 60;
  io.Fonts->AddFontDefault();
  unsigned char *pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  try {
    check(root);
    ImGui::DestroyContext();
    fs::remove_all(root);
    std::cout << "Hierarchy reveal checks passed\n";
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    ImGui::DestroyContext();
    fs::remove_all(root);
    return 1;
  }
}
