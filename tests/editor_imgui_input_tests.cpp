#include "editor/EditorImGuiInput.h"
#include "editor/EditorStructuredValue.h"
#include <nlohmann/json.hpp>

#include "demi/runtime/scene/model/SceneTypes.h"

#include <imgui.h>

#ifdef NDEBUG
#undef NDEBUG
#endif
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

  // Exercise structured array/object authoring through actual ImGui events,
  // without a desktop window or the editor's document/renderer machinery.
  io.AddKeyEvent(ImGuiKey_LeftShift, false);
  ImVec2 addButton;
  const auto draw = [&](nlohmann::json &value, int width, bool readOnly) {
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0,0});
    ImGui::SetNextWindowSize({640,480});
    ImGui::Begin("Structured input", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
    ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    const auto edit = demi::editor::drawStructuredValue(value,width,readOnly);
    const auto a=ImGui::GetItemRectMin(), b=ImGui::GetItemRectMax();
    addButton={(a.x+b.x)/2,(a.y+b.y)/2};
    ImGui::End();
    ImGui::Render();
    return edit;
  };
  auto vectors = nlohmann::json::array();
  draw(vectors,2,false);
  io.AddMousePosEvent(addButton.x,addButton.y);
  io.AddMouseButtonEvent(0,true); draw(vectors,2,false);
  io.AddMouseButtonEvent(0,false);
  assert(draw(vectors,2,false).changed);
  assert(vectors==nlohmann::json::array({{0,0}}));
  io.AddMousePosEvent(addButton.x,addButton.y);
  io.AddMouseButtonEvent(0,true); draw(vectors,2,true);
  io.AddMouseButtonEvent(0,false);
  assert(!draw(vectors,2,true).changed && vectors.size()==1);
  auto object=nlohmann::json::object();
  draw(object,0,false);
  io.AddMousePosEvent(addButton.x,addButton.y);
  io.AddMouseButtonEvent(0,true); draw(object,0,false);
  io.AddMouseButtonEvent(0,false);
  assert(draw(object,0,false).changed);
  assert(object.contains("new_field") && object["new_field"].is_null());
  ImGui::DestroyContext();
  return 0;
}
