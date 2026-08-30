#include "editor/EditorImGuiInput.h"

#include "demi/runtime/scene/model/SceneTypes.h"

#include <imgui.h>

#include <string>
#include <string_view>
#include <unordered_map>

namespace demi::editor {
namespace {

ImGuiKey imguiKey(const std::string_view key) {
  static const std::unordered_map<std::string_view, ImGuiKey> Keys{
      {"tab", ImGuiKey_Tab},
      {"left", ImGuiKey_LeftArrow},
      {"right", ImGuiKey_RightArrow},
      {"up", ImGuiKey_UpArrow},
      {"down", ImGuiKey_DownArrow},
      {"home", ImGuiKey_Home},
      {"end", ImGuiKey_End},
      {"delete", ImGuiKey_Delete},
      {"backspace", ImGuiKey_Backspace},
      {"space", ImGuiKey_Space},
      {"return", ImGuiKey_Enter},
      {"escape", ImGuiKey_Escape},
      {"a", ImGuiKey_A},
      {"c", ImGuiKey_C},
      {"d", ImGuiKey_D},
      {"e", ImGuiKey_E},
      {"f", ImGuiKey_F},
      {"n", ImGuiKey_N},
      {"q", ImGuiKey_Q},
      {"s", ImGuiKey_S},
      {"v", ImGuiKey_V},
      {"w", ImGuiKey_W},
      {"x", ImGuiKey_X},
      {"y", ImGuiKey_Y},
      {"z", ImGuiKey_Z},
      {"f1", ImGuiKey_F1},
      {"f2", ImGuiKey_F2},
      {"f3", ImGuiKey_F3},
      {"f4", ImGuiKey_F4},
      {"f5", ImGuiKey_F5},
      {"f6", ImGuiKey_F6},
      {"f7", ImGuiKey_F7},
      {"f8", ImGuiKey_F8},
      {"f9", ImGuiKey_F9},
      {"f10", ImGuiKey_F10},
      {"f11", ImGuiKey_F11},
      {"f12", ImGuiKey_F12},
  };
  const auto found = Keys.find(key);
  return found == Keys.end() ? ImGuiKey_None : found->second;
}

} // namespace

void submitEditorImGuiInput(const runtime::InputState &input) {
  ImGuiIO &io = ImGui::GetIO();
  io.AddMouseWheelEvent(input.mouseScroll.x, input.mouseScroll.y);
  for (const std::string &key : input.keysPressed)
    if (const ImGuiKey mapped = imguiKey(key); mapped != ImGuiKey_None)
      io.AddKeyEvent(mapped, true);
  for (const std::string &key : input.keysReleased)
    if (const ImGuiKey mapped = imguiKey(key); mapped != ImGuiKey_None)
      io.AddKeyEvent(mapped, false);
  const auto down = [&input](const std::string_view left,
                             const std::string_view right) {
    return input.keysDown.contains(std::string(left)) ||
           input.keysDown.contains(std::string(right));
  };
  io.AddKeyEvent(ImGuiMod_Ctrl, down("left ctrl", "right ctrl"));
  io.AddKeyEvent(ImGuiMod_Shift, down("left shift", "right shift"));
  io.AddKeyEvent(ImGuiMod_Alt, down("left alt", "right alt"));
  if (!input.textEntered.empty())
    io.AddInputCharactersUTF8(input.textEntered.c_str());
}

} // namespace demi::editor
