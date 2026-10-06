#include "editor/EditorPaletteCard.h"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <imgui.h>
#include <imgui_internal.h>

int main() {
  ImGui::SetAllocatorFunctions(
      [](std::size_t size, void *) { return std::malloc(size); },
      [](void *value, void *) { std::free(value); });
  ImGui::CreateContext();
  auto &io = ImGui::GetIO();
  io.IniFilename = nullptr;
  io.DisplaySize = {800, 600};
  io.DeltaTime = 1.0F / 60.0F;
  io.Fonts->AddFontDefault();
  unsigned char *pixels;
  int width, height;
  io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

  const demi::editor::EditorPaletteCard card{
      "material:metal", "Metal material",
      "Drag this material onto an asset slot.", demi::editor::EditorIcon::File};
  const char reference[] = "asset://materials/metal";
  const auto payload = std::as_bytes(std::span(reference));
  ImVec2 point;
  const auto frame = [&](ImVec2 mouse, bool down) {
    io.AddMousePosEvent(mouse.x, mouse.y);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left, down);
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({360, 500}, ImGuiCond_Always);
    ImGui::Begin("Independent asset palette");
    const auto start = ImGui::GetCursorScreenPos();
    point = {start.x + 30, start.y + 48};
    (void)demi::editor::drawEditorPaletteCard(card, "EDITOR_MATERIAL", payload);
    ImGui::End();
    ImGui::Render();
    assert(ImGui::GetCurrentContext()->ErrorCountCurrentFrame == 0);
  };
  frame({500, 500}, false);
  frame(point, false);
  frame(point, true);
  frame({point.x + 20, point.y}, true);
  const auto *drag = ImGui::GetDragDropPayload();
  assert(drag && drag->IsDataType("EDITOR_MATERIAL"));
  assert(drag->DataSize == sizeof(reference));
  assert(std::memcmp(drag->Data, reference, sizeof(reference)) == 0);
  frame(point, false);
  ImGui::NewFrame();
  ImGui::Begin("Non-draggable palette entry");
  (void)demi::editor::drawEditorPaletteCard(card);
  ImGui::End();
  ImGui::Render();
  assert(ImGui::GetCurrentContext()->ErrorCountCurrentFrame == 0);
  ImGui::DestroyContext();
}
