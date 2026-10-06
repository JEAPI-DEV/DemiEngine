#include "editor/EditorGraphCanvasScale.h"
#include <imgui.h>

namespace demi::editor {
EditorGraphCanvasScale::EditorGraphCanvasScale(float zoom)
    : nodeStyle_(ImNodes::GetStyle()) {
  const auto &style = ImGui::GetStyle();
  ImGui::PushFont(nullptr, style.FontSizeBase * zoom);
  ImGui::PushStyleVar(
      ImGuiStyleVar_ItemSpacing,
      ImVec2{style.ItemSpacing.x * zoom, style.ItemSpacing.y * zoom});
  ImGui::PushStyleVar(
      ImGuiStyleVar_ItemInnerSpacing,
      ImVec2{style.ItemInnerSpacing.x * zoom, style.ItemInnerSpacing.y * zoom});
  ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, style.IndentSpacing * zoom);
  auto &nodes = ImNodes::GetStyle();
  nodes.GridSpacing *= zoom;
  nodes.NodeCornerRounding *= zoom;
  nodes.NodePadding = {nodes.NodePadding.x * zoom, nodes.NodePadding.y * zoom};
  nodes.NodeBorderThickness *= zoom;
  nodes.LinkThickness *= zoom;
  nodes.LinkHoverDistance *= zoom;
  nodes.PinCircleRadius *= zoom;
  nodes.PinQuadSideLength *= zoom;
  nodes.PinTriangleSideLength *= zoom;
  nodes.PinLineThickness *= zoom;
  nodes.PinHoverRadius *= zoom;
  nodes.PinOffset *= zoom;
}
EditorGraphCanvasScale::~EditorGraphCanvasScale() {
  ImNodes::GetStyle() = nodeStyle_;
  ImGui::PopStyleVar(3);
  ImGui::PopFont();
}
} // namespace demi::editor
