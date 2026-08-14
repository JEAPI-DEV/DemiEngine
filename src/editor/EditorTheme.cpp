#include "editor/EditorTheme.h"

#include <imgui.h>

namespace demi::editor {

void applyEditorTheme() {
  ImGuiStyle &style = ImGui::GetStyle();
  style.WindowPadding = {10.0F, 9.0F};
  style.FramePadding = {8.0F, 5.0F};
  style.CellPadding = {7.0F, 5.0F};
  style.ItemSpacing = {7.0F, 6.0F};
  style.ItemInnerSpacing = {5.0F, 4.0F};
  style.ScrollbarSize = 12.0F;
  style.GrabMinSize = 9.0F;
  style.WindowBorderSize = 1.0F;
  style.ChildBorderSize = 1.0F;
  style.PopupBorderSize = 1.0F;
  style.FrameBorderSize = 1.0F;
  style.TabBorderSize = 0.0F;
  style.WindowRounding = 3.0F;
  style.ChildRounding = 3.0F;
  style.FrameRounding = 3.0F;
  style.PopupRounding = 3.0F;
  style.ScrollbarRounding = 8.0F;
  style.GrabRounding = 3.0F;
  style.TabRounding = 3.0F;

  ImVec4 *colors = style.Colors;
  colors[ImGuiCol_Text] = {0.88F, 0.89F, 0.92F, 1.00F};
  colors[ImGuiCol_TextDisabled] = {0.46F, 0.48F, 0.54F, 1.00F};
  colors[ImGuiCol_WindowBg] = {0.075F, 0.082F, 0.098F, 1.00F};
  colors[ImGuiCol_ChildBg] = {0.085F, 0.092F, 0.108F, 1.00F};
  colors[ImGuiCol_PopupBg] = {0.095F, 0.102F, 0.120F, 0.98F};
  colors[ImGuiCol_Border] = {0.18F, 0.19F, 0.22F, 1.00F};
  colors[ImGuiCol_BorderShadow] = {0.0F, 0.0F, 0.0F, 0.0F};
  colors[ImGuiCol_FrameBg] = {0.115F, 0.122F, 0.14F, 1.00F};
  colors[ImGuiCol_FrameBgHovered] = {0.17F, 0.16F, 0.23F, 1.00F};
  colors[ImGuiCol_FrameBgActive] = {0.25F, 0.20F, 0.38F, 1.00F};
  colors[ImGuiCol_TitleBg] = {0.065F, 0.070F, 0.083F, 1.00F};
  colors[ImGuiCol_TitleBgActive] = {0.085F, 0.090F, 0.11F, 1.00F};
  colors[ImGuiCol_MenuBarBg] = {0.09F, 0.095F, 0.11F, 1.00F};
  colors[ImGuiCol_ScrollbarBg] = {0.065F, 0.07F, 0.08F, 1.00F};
  colors[ImGuiCol_ScrollbarGrab] = {0.22F, 0.23F, 0.27F, 1.00F};
  colors[ImGuiCol_ScrollbarGrabHovered] = {0.31F, 0.28F, 0.40F, 1.00F};
  colors[ImGuiCol_CheckMark] = {0.60F, 0.43F, 0.95F, 1.00F};
  colors[ImGuiCol_SliderGrab] = {0.50F, 0.34F, 0.82F, 1.00F};
  colors[ImGuiCol_SliderGrabActive] = {0.68F, 0.50F, 1.00F, 1.00F};
  colors[ImGuiCol_Button] = {0.16F, 0.15F, 0.21F, 1.00F};
  colors[ImGuiCol_ButtonHovered] = {0.31F, 0.23F, 0.48F, 1.00F};
  colors[ImGuiCol_ButtonActive] = {0.41F, 0.29F, 0.64F, 1.00F};
  colors[ImGuiCol_Header] = {0.25F, 0.19F, 0.37F, 1.00F};
  colors[ImGuiCol_HeaderHovered] = {0.34F, 0.25F, 0.52F, 1.00F};
  colors[ImGuiCol_HeaderActive] = {0.43F, 0.31F, 0.66F, 1.00F};
  colors[ImGuiCol_Separator] = {0.19F, 0.20F, 0.23F, 1.00F};
  colors[ImGuiCol_ResizeGrip] = {0.42F, 0.30F, 0.64F, 0.25F};
  colors[ImGuiCol_ResizeGripHovered] = {0.54F, 0.38F, 0.82F, 0.65F};
  colors[ImGuiCol_Tab] = {0.10F, 0.105F, 0.125F, 1.00F};
  colors[ImGuiCol_TabHovered] = {0.29F, 0.22F, 0.43F, 1.00F};
  colors[ImGuiCol_TabSelected] = {0.22F, 0.17F, 0.34F, 1.00F};
  colors[ImGuiCol_NavCursor] = {0.64F, 0.46F, 0.96F, 1.00F};
}

} // namespace demi::editor
