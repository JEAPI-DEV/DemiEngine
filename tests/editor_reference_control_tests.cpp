#include "editor/EditorReferenceControl.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <vector>

int main() {
  ImGui::SetAllocatorFunctions(
      [](std::size_t size, void *) { return std::malloc(size); },
      [](void *memory, void *) { std::free(memory); });
  ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = {800, 600};
  io.DeltaTime = 1.F / 60.F;
  io.Fonts->AddFontDefault();
  unsigned char *pixels;
  int width;
  int height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
  int preparations = 0;
  std::string selected = "unknown";
  const auto choices = [&] {
    ++preparations;
    return std::vector<demi::editor::EditorReferenceChoice>{
        {"known", "Known entity"}};
  };
  const auto frame = [&] {
    ImGui::NewFrame();
    ImGui::SetNextWindowSize({400, 300}, ImGuiCond_Always);
    ImGui::Begin("Reference probe");
    const bool changed = demi::editor::drawEditorLazyReferenceControl(
        "Reference", choices, selected, true);
    ImGui::End();
    ImGui::Render();
    assert(!changed);
  };
  frame();
  frame();
  assert(preparations == 0 && selected == "unknown");
  auto *window = ImGui::FindWindowByName("Reference probe");
  ImGui::ActivateItemByID(window->GetID("Reference"));
  frame();
  assert(preparations == 1 && selected == "unknown");
  frame();
  assert(preparations == 2);
  // Close the popup directly: Escape first leaves the search field's editing
  // state, which is a separate input behavior from deferred option preparation.
  ImGui::ClosePopupToLevel(0, true);
  frame();
  const int afterClose = preparations;
  frame();
  assert(preparations == afterClose);
  ImGui::DestroyContext();
}
