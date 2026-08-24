#include "editor/EditorChrome.h"

#include "editor/EditorPanelStyle.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace demi::editor {
namespace {

ImVec2 point(const ImVec2 center, const float x, const float y,
             const float scale) {
  return {center.x + x * scale, center.y + y * scale};
}

void arrowHead(ImDrawList &draw, const ImVec2 tip, const ImVec2 direction,
               const ImU32 color, const float scale) {
  const ImVec2 normal{-direction.y, direction.x};
  const ImVec2 base{tip.x - direction.x * 4.0F * scale,
                    tip.y - direction.y * 4.0F * scale};
  draw.AddTriangleFilled(
      tip, {base.x + normal.x * 2.5F * scale, base.y + normal.y * 2.5F * scale},
      {base.x - normal.x * 2.5F * scale, base.y - normal.y * 2.5F * scale},
      color);
}

} // namespace

void drawEditorGlyph(ImDrawList &draw, const EditorIcon icon,
                     const ImVec2 center, const ImU32 color,
                     const float scale) {
  constexpr float Thickness = 1.6F;
  const float thickness = Thickness * scale;
  switch (icon) {
  case EditorIcon::Refresh: {
    draw.PathArcTo(center, 7.0F * scale, -2.7F, 2.1F, 18);
    draw.PathStroke(color, 0, thickness);
    arrowHead(draw, point(center, -6.3F, -3.0F, scale), {-1.0F, 0.0F}, color,
              scale);
    break;
  }
  case EditorIcon::Undo:
  case EditorIcon::Redo: {
    const float direction = icon == EditorIcon::Undo ? -1.0F : 1.0F;
    const ImVec2 tip = point(center, 7.5F * direction, -2.5F, scale);
    draw.PathLineTo(tip);
    draw.PathLineTo(point(center, 2.5F * direction, -7.0F, scale));
    draw.PathBezierCubicCurveTo(point(center, -5.5F * direction, -7.0F, scale),
                                point(center, -7.0F * direction, -1.0F, scale),
                                point(center, -7.0F * direction, 6.0F, scale));
    draw.PathStroke(color, 0, thickness);
    arrowHead(draw, tip, {direction, 0.0F}, color, scale * 0.9F);
    break;
  }
  case EditorIcon::Save:
    draw.AddRect(point(center, -7.0F, -7.0F, scale),
                 point(center, 7.0F, 7.0F, scale), color, 1.0F, 0, thickness);
    draw.AddRect(point(center, -3.5F, -7.0F, scale),
                 point(center, 4.0F, -1.5F, scale), color, 0.0F, 0, thickness);
    draw.AddRect(point(center, -4.0F, 2.0F, scale),
                 point(center, 4.0F, 7.0F, scale), color, 0.0F, 0, thickness);
    break;
  case EditorIcon::Play:
    draw.AddTriangleFilled(point(center, -4.5F, -7.0F, scale),
                           point(center, 7.0F, 0.0F, scale),
                           point(center, -4.5F, 7.0F, scale), color);
    break;
  case EditorIcon::Pause:
    draw.AddRectFilled(point(center, -6.0F, -7.0F, scale),
                       point(center, -2.0F, 7.0F, scale), color);
    draw.AddRectFilled(point(center, 2.0F, -7.0F, scale),
                       point(center, 6.0F, 7.0F, scale), color);
    break;
  case EditorIcon::Stop:
    draw.AddRectFilled(point(center, -6.0F, -6.0F, scale),
                       point(center, 6.0F, 6.0F, scale), color, 1.0F);
    break;
  case EditorIcon::Pointer:
    draw.AddTriangle(point(center, -6.0F, -7.0F, scale),
                     point(center, 5.0F, 2.0F, scale),
                     point(center, -1.0F, 3.0F, scale), color, thickness);
    draw.AddLine(point(center, -1.0F, 3.0F, scale),
                 point(center, 4.5F, 8.0F, scale), color, thickness);
    break;
  case EditorIcon::Move:
    draw.AddLine(point(center, -8.0F, 0.0F, scale),
                 point(center, 8.0F, 0.0F, scale), color, thickness);
    draw.AddLine(point(center, 0.0F, -8.0F, scale),
                 point(center, 0.0F, 8.0F, scale), color, thickness);
    for (const auto &[tip, direction] :
         std::array<std::pair<ImVec2, ImVec2>, 4>{
             std::pair{point(center, -8.0F, 0.0F, scale), ImVec2{-1.0F, 0.0F}},
             std::pair{point(center, 8.0F, 0.0F, scale), ImVec2{1.0F, 0.0F}},
             std::pair{point(center, 0.0F, -8.0F, scale), ImVec2{0.0F, -1.0F}},
             std::pair{point(center, 0.0F, 8.0F, scale), ImVec2{0.0F, 1.0F}}})
      arrowHead(draw, tip, direction, color, scale * 0.8F);
    break;
  case EditorIcon::Rotate:
    draw.PathArcTo(center, 7.0F * scale, -2.6F, 2.4F, 22);
    draw.PathStroke(color, 0, thickness);
    arrowHead(draw, point(center, -6.0F, -3.5F, scale), {-0.8F, -0.5F}, color,
              scale);
    break;
  case EditorIcon::Scale:
    draw.AddLine(point(center, -5.0F, 5.0F, scale),
                 point(center, 5.0F, -5.0F, scale), color, thickness);
    draw.AddRect(point(center, -8.0F, 3.0F, scale),
                 point(center, -3.0F, 8.0F, scale), color, 0.0F, 0, thickness);
    draw.AddRect(point(center, 3.0F, -8.0F, scale),
                 point(center, 8.0F, -3.0F, scale), color, 0.0F, 0, thickness);
    break;
  case EditorIcon::Frame:
    for (const float sx : {-1.0F, 1.0F})
      for (const float sy : {-1.0F, 1.0F}) {
        const ImVec2 corner = point(center, sx * 7.0F, sy * 7.0F, scale);
        draw.AddLine(corner, point(corner, -sx * 5.0F, 0.0F, scale), color,
                     thickness);
        draw.AddLine(corner, point(corner, 0.0F, -sy * 5.0F, scale), color,
                     thickness);
      }
    break;
  case EditorIcon::Camera:
    draw.AddRect(point(center, -7.0F, -5.0F, scale),
                 point(center, 4.0F, 6.0F, scale), color, 1.0F, 0, thickness);
    draw.AddTriangle(point(center, 4.0F, -3.0F, scale),
                     point(center, 8.0F, -6.0F, scale),
                     point(center, 8.0F, 5.0F, scale), color, thickness);
    break;
  case EditorIcon::Grid:
    for (const float offset : {-5.0F, 0.0F, 5.0F}) {
      draw.AddLine(point(center, -7.0F, offset, scale),
                   point(center, 7.0F, offset, scale), color, thickness);
      draw.AddLine(point(center, offset, -7.0F, scale),
                   point(center, offset, 7.0F, scale), color, thickness);
    }
    break;
  case EditorIcon::Eye:
    draw.AddBezierCubic(point(center, -8.0F, 0.0F, scale),
                        point(center, -3.0F, -7.0F, scale),
                        point(center, 3.0F, -7.0F, scale),
                        point(center, 8.0F, 0.0F, scale), color, thickness);
    draw.AddBezierCubic(point(center, 8.0F, 0.0F, scale),
                        point(center, 3.0F, 7.0F, scale),
                        point(center, -3.0F, 7.0F, scale),
                        point(center, -8.0F, 0.0F, scale), color, thickness);
    draw.AddCircleFilled(center, 2.5F * scale, color);
    break;
  case EditorIcon::Hud:
    draw.AddRect(point(center, -7.5F, -6.5F, scale),
                 point(center, 7.5F, 6.5F, scale), color, 1.5F, 0, thickness);
    draw.AddLine(point(center, -7.5F, -2.5F, scale),
                 point(center, 7.5F, -2.5F, scale), color, thickness);
    draw.AddCircleFilled(point(center, -4.8F, -4.5F, scale), 0.9F * scale,
                         color);
    draw.AddRect(point(center, -4.5F, 0.0F, scale),
                 point(center, 4.5F, 3.5F, scale), color, 0.5F, 0, thickness);
    break;
  case EditorIcon::Folder:
    draw.AddRectFilled(point(center, -8.0F, -3.0F, scale),
                       point(center, 8.0F, 7.0F, scale), color, 1.5F);
    draw.AddRectFilled(point(center, -7.0F, -7.0F, scale),
                       point(center, 0.0F, -2.0F, scale), color, 1.5F);
    break;
  case EditorIcon::File:
    draw.AddRect(point(center, -6.0F, -8.0F, scale),
                 point(center, 6.0F, 8.0F, scale), color, 1.0F, 0, thickness);
    draw.AddLine(point(center, 1.0F, -8.0F, scale),
                 point(center, 6.0F, -3.0F, scale), color, thickness);
    draw.AddLine(point(center, 1.0F, -8.0F, scale),
                 point(center, 1.0F, -3.0F, scale), color, thickness);
    draw.AddLine(point(center, 1.0F, -3.0F, scale),
                 point(center, 6.0F, -3.0F, scale), color, thickness);
    break;
  case EditorIcon::Add:
    draw.AddLine(point(center, -7.0F, 0.0F, scale),
                 point(center, 7.0F, 0.0F, scale), color, thickness);
    draw.AddLine(point(center, 0.0F, -7.0F, scale),
                 point(center, 0.0F, 7.0F, scale), color, thickness);
    break;
  case EditorIcon::Settings:
    draw.AddCircle(center, 6.0F * scale, color, 16, thickness);
    draw.AddCircleFilled(center, 2.0F * scale, color);
    for (int index = 0; index < 8; ++index) {
      const float angle = static_cast<float>(index) * 0.785398F;
      draw.AddLine({center.x + std::cos(angle) * 6.0F * scale,
                    center.y + std::sin(angle) * 6.0F * scale},
                   {center.x + std::cos(angle) * 8.5F * scale,
                    center.y + std::sin(angle) * 8.5F * scale},
                   color, thickness);
    }
    break;
  }
}

bool editorIconButton(const char *id, const EditorIcon icon,
                      const char *tooltip, const bool selected,
                      const bool enabled, const ImVec2 size) {
  if (!enabled)
    ImGui::BeginDisabled();
  const bool pressed = ImGui::InvisibleButton(id, size);
  const bool hovered =
      ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
  const bool held = ImGui::IsItemActive();
  const ImVec2 min = ImGui::GetItemRectMin();
  const ImVec2 max = ImGui::GetItemRectMax();
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const ImU32 background = selected || held     ? IM_COL32(79, 57, 126, 255)
                           : hovered && enabled ? IM_COL32(52, 54, 62, 255)
                                                : IM_COL32(37, 39, 46, 255);
  const ImU32 border = selected ? EditorAccent : IM_COL32(61, 64, 72, 255);
  const ImU32 foreground =
      enabled ? IM_COL32(220, 223, 230, 255) : IM_COL32(105, 108, 119, 255);
  draw->AddRectFilled(min, max, background, 2.0F);
  draw->AddRect(min, max, border, 2.0F);
  drawEditorGlyph(*draw, icon, {(min.x + max.x) * 0.5F, (min.y + max.y) * 0.5F},
                  foreground);
  if (!enabled)
    ImGui::EndDisabled();
  if (hovered && tooltip != nullptr)
    ImGui::SetTooltip("%s", tooltip);
  return enabled && pressed;
}

void editorToolbarSeparator(const float height) {
  ImGui::SameLine();
  const ImVec2 cursor = ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddLine(
      {cursor.x + 2.0F, cursor.y + 3.0F},
      {cursor.x + 2.0F, cursor.y + height - 3.0F}, IM_COL32(64, 66, 74, 255));
  ImGui::Dummy({7.0F, height});
  ImGui::SameLine();
}

bool editorStageTab(const char *label, const bool selected, const ImVec2 size) {
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0F);
  ImGui::PushStyleColor(ImGuiCol_Button,
                        selected ? ImVec4{0.15F, 0.16F, 0.19F, 1.0F}
                                 : ImVec4{0.09F, 0.095F, 0.11F, 1.0F});
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, {0.18F, 0.18F, 0.22F, 1.0F});
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, {0.20F, 0.18F, 0.27F, 1.0F});
  const bool pressed = ImGui::Button(label, size);
  if (selected) {
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddLine(
        {min.x, max.y - 1.0F}, {max.x, max.y - 1.0F}, EditorAccent, 2.0F);
  }
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar();
  return pressed;
}

} // namespace demi::editor
