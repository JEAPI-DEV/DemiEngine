#include "editor/EditorTheme.h"

#include <imgui.h>

namespace demi::editor {

void applyEditorTheme() {
  ImGuiStyle &style = ImGui::GetStyle();
  style.WindowPadding = {8.0F, 7.0F};
  style.FramePadding = {7.0F, 4.0F};
  style.CellPadding = {6.0F, 4.0F};
  style.ItemSpacing = {5.0F, 4.0F};
  style.ItemInnerSpacing = {4.0F, 3.0F};
  style.ScrollbarSize = 11.0F;
  style.GrabMinSize = 8.0F;
  style.WindowBorderSize = 1.0F;
  style.ChildBorderSize = 1.0F;
  style.PopupBorderSize = 1.0F;
  style.FrameBorderSize = 1.0F;
  style.TabBorderSize = 0.0F;
  style.WindowRounding = 1.0F;
  style.ChildRounding = 1.0F;
  style.FrameRounding = 2.0F;
  style.PopupRounding = 2.0F;
  style.ScrollbarRounding = 5.0F;
  style.GrabRounding = 2.0F;
  style.TabRounding = 2.0F;

  ImVec4 *colors = style.Colors;
  colors[ImGuiCol_Text] = {0.84F, 0.85F, 0.88F, 1.00F};
  colors[ImGuiCol_TextDisabled] = {0.47F, 0.49F, 0.55F, 1.00F};
  colors[ImGuiCol_WindowBg] = {0.066F, 0.070F, 0.081F, 1.00F};
  colors[ImGuiCol_ChildBg] = {0.075F, 0.079F, 0.091F, 1.00F};
  colors[ImGuiCol_PopupBg] = {0.083F, 0.088F, 0.103F, 0.99F};
  colors[ImGuiCol_Border] = {0.145F, 0.153F, 0.175F, 1.00F};
  colors[ImGuiCol_BorderShadow] = {0.0F, 0.0F, 0.0F, 0.0F};
  colors[ImGuiCol_FrameBg] = {0.105F, 0.110F, 0.126F, 1.00F};
  colors[ImGuiCol_FrameBgHovered] = {0.145F, 0.145F, 0.174F, 1.00F};
  colors[ImGuiCol_FrameBgActive] = {0.20F, 0.17F, 0.29F, 1.00F};
  colors[ImGuiCol_TitleBg] = {0.055F, 0.058F, 0.068F, 1.00F};
  colors[ImGuiCol_TitleBgActive] = {0.071F, 0.075F, 0.087F, 1.00F};
  colors[ImGuiCol_MenuBarBg] = {0.072F, 0.076F, 0.088F, 1.00F};
  colors[ImGuiCol_ScrollbarBg] = {0.052F, 0.055F, 0.064F, 1.00F};
  colors[ImGuiCol_ScrollbarGrab] = {0.19F, 0.20F, 0.23F, 1.00F};
  colors[ImGuiCol_ScrollbarGrabHovered] = {0.27F, 0.25F, 0.34F, 1.00F};
  colors[ImGuiCol_CheckMark] = {0.60F, 0.43F, 0.95F, 1.00F};
  colors[ImGuiCol_SliderGrab] = {0.50F, 0.34F, 0.82F, 1.00F};
  colors[ImGuiCol_SliderGrabActive] = {0.68F, 0.50F, 1.00F, 1.00F};
  colors[ImGuiCol_Button] = {0.115F, 0.119F, 0.139F, 1.00F};
  colors[ImGuiCol_ButtonHovered] = {0.185F, 0.166F, 0.255F, 1.00F};
  colors[ImGuiCol_ButtonActive] = {0.30F, 0.22F, 0.48F, 1.00F};
  colors[ImGuiCol_Header] = {0.235F, 0.185F, 0.34F, 1.00F};
  colors[ImGuiCol_HeaderHovered] = {0.29F, 0.225F, 0.43F, 1.00F};
  colors[ImGuiCol_HeaderActive] = {0.37F, 0.275F, 0.56F, 1.00F};
  colors[ImGuiCol_Separator] = {0.145F, 0.153F, 0.175F, 1.00F};
  colors[ImGuiCol_ResizeGrip] = {0.42F, 0.30F, 0.64F, 0.25F};
  colors[ImGuiCol_ResizeGripHovered] = {0.54F, 0.38F, 0.82F, 0.65F};
  colors[ImGuiCol_Tab] = {0.075F, 0.079F, 0.092F, 1.00F};
  colors[ImGuiCol_TabHovered] = {0.19F, 0.16F, 0.27F, 1.00F};
  colors[ImGuiCol_TabSelected] = {0.16F, 0.14F, 0.22F, 1.00F};
  colors[ImGuiCol_NavCursor] = {0.64F, 0.46F, 0.96F, 1.00F};
}

} // namespace demi::editor
