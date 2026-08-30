#include "editor/EditorImGuiInput.h"

#include "demi/runtime/scene/model/SceneTypes.h"

#include <imgui.h>

#include <cassert>
#include <cmath>
#include <cstdlib>

namespace {

void *allocate(const std::size_t size, void *) { return std::malloc(size); }

void release(void *memory, void *) { std::free(memory); }

} // namespace

int main() {
  ImGui::SetAllocatorFunctions(allocate, release);
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize = {640.0F, 480.0F};
  io.DeltaTime = 1.0F / 60.0F;
  io.IniFilename = nullptr;
  io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;

  demi::runtime::InputState input;
  input.mouseScroll = {0.25F, -1.5F};
  input.keysPressed.insert("n");
  input.keysDown.insert("left shift");
  demi::editor::submitEditorImGuiInput(input);
  ImGui::NewFrame();

  assert(std::abs(io.MouseWheelH - 0.25F) < 0.001F);
  assert(std::abs(io.MouseWheel + 1.5F) < 0.001F);
  assert(ImGui::IsKeyDown(ImGuiKey_N));
  assert(io.KeyShift);

  ImGui::EndFrame();
  ImGui::DestroyContext();
  return 0;
}
