#include "editor/EditorImGuiInput.h"
#include "editor/EditorInputOwnership.h"
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
  {
    demi::editor::EditorInputOwnership ownership;
    demi::runtime::InputState raw;
    raw.mousePosition={45,45};raw.mouseDelta={400,500};raw.mouseScroll={0,2};
    raw.mouseButtonsDown={"left"};raw.mouseButtonsPressed={"left"};
    raw.keysDown={"left ctrl","s","d"};raw.keysPressed={"s","d"};raw.textEntered="hidden input";
    const auto captured=ownership.route(raw,true);
    assert(captured.changed && captured.exclusive && captured.input.mouseButtonsDown.empty());
    assert(captured.input.mouseDelta.x==0 && captured.input.mouseScroll.y==0 && captured.input.textEntered.empty());
    assert(!captured.input.keysPressed.contains("s") && captured.input.keysPressed.contains("d"));
    assert(captured.input.keysDown.contains("left ctrl"));
    const auto released=ownership.route(raw,false);
    assert(released.changed && released.input.mouseButtonsDown.empty() && released.input.keysDown.empty());
    assert(released.input.textEntered.empty());
    raw={};ownership.route(raw,false);
    raw.mouseButtonsDown={"left"};raw.mouseButtonsPressed={"left"};
    assert(ownership.route(raw,false).input.mouseButtonsDown.contains("left"));

    demi::editor::EditorInputOwnership clickOwnership;
    const auto buttonFrame=[&](bool exclusive,bool down) {
      demi::runtime::InputState pointer;pointer.mousePosition={45,45};
      if(down)pointer.mouseButtonsDown.insert("left");
      auto routed=clickOwnership.route(pointer,exclusive);
      if(routed.changed){io.ClearInputKeys();io.ClearInputMouse();}
      io.AddMousePosEvent(exclusive?-3.4e38F:45,exclusive?-3.4e38F:45);
      io.AddMouseButtonEvent(0,routed.input.mouseButtonsDown.contains("left"));
      ImGui::NewFrame();
      ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({200,200});
      ImGui::Begin("capture-test",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize);
      ImGui::SetCursorScreenPos({20,20});
      const bool clicked=ImGui::Button("editor control",{100,60});
      ImGui::End();ImGui::EndFrame();return clicked;
    };
    assert(!buttonFrame(false,false));
    assert(!buttonFrame(true,true));
    assert(!buttonFrame(false,true)); // No held-button replay into editor.
    assert(!buttonFrame(false,false));
    assert(!buttonFrame(false,true));
    assert(buttonFrame(false,false));
  }
  ImGui::DestroyContext();
  return 0;
}
