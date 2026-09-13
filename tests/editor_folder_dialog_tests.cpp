#include "editor/EditorAssetDialogs.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>

int main() {
  namespace fs = std::filesystem;
  const auto root =
      fs::temp_directory_path() /
      ("demi-folder-dialog-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  fs::create_directories(root / "scenes");
  {
    std::ofstream project(root / "demi.project.json");
    project
        << R"({"format_version":1,"name":"Folders","main_scene":"scene://main","scenes":[{"id":"scene://main","path":"scenes/main.scene.json"}]})";
    std::ofstream scene(root / "scenes/main.scene.json");
    scene << R"({"format_version":1,"id":"scene://main","entities":[]})";
  }
  demi::editor::EditorWorkspace workspace;
  std::string error;
  assert(workspace.open(root, error));
  ImGui::SetAllocatorFunctions(
      [](std::size_t size, void *) { return std::malloc(size); },
      [](void *pointer, void *) { std::free(pointer); });
  ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = {960, 540};
  io.DeltaTime = 1.0F / 60.0F;
  unsigned char *pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  demi::editor::EditorAssetDialogs dialogs;
  std::string notice;
  const auto frame = [&] {
    ImGui::NewFrame();
    dialogs.draw(workspace, notice);
    ImGui::Render();
  };
  const auto openAndType = [&](const char *name) {
    dialogs.openNewFolder("scenes");
    frame();
    frame();
    io.AddInputCharactersUTF8(name);
    frame();
  };
  const auto key = [&](ImGuiKey value) {
    io.AddKeyEvent(value, true);
    frame();
    io.AddKeyEvent(value, false);
    frame();
  };
  openAndType("Empty UI folder");
  key(ImGuiKey_Enter);
  assert(dialogs.takeCreatedFolder() == fs::path("scenes/Empty UI folder"));
  assert(workspace.sourceDirectories().contains("scenes/Empty UI folder"));
  assert(fs::is_empty(root / "scenes/Empty UI folder"));
  openAndType("Empty UI folder");
  key(ImGuiKey_Enter);
  assert(!dialogs.takeCreatedFolder());
  assert(notice.find("already exists") != std::string::npos);
  key(ImGuiKey_Escape);
  openAndType("Cancelled");
  key(ImGuiKey_Escape);
  assert(!dialogs.takeCreatedFolder());
  assert(!fs::exists(root / "scenes/Cancelled"));
  ImGui::DestroyContext();
  fs::remove_all(root);
}
