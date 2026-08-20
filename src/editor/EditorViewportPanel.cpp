#include "editor/EditorViewportPanel.h"

#include "editor/EditorIsoGridCell.h"
#include "editor/EditorPanelStyle.h"
#include "editor/EditorViewportOverlay2D.h"
#include "editor/EditorViewportProjection.h"
#include "editor/EditorWorkspace.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace demi::editor {
namespace {

void drawOrientationGizmo(ImDrawList &draw, const ImVec2 center,
                          const EditorSceneViewCamera &camera) {
  struct Axis {
    runtime::Vec3 worldDirection;
    ImU32 color;
    const char *label;
  };
  constexpr std::array Axes{
      Axis{{1.0F, 0.0F, 0.0F}, IM_COL32(239, 79, 104, 255), "X"},
      Axis{{0.0F, 1.0F, 0.0F}, IM_COL32(91, 215, 125, 255), "Y"},
      Axis{{0.0F, 0.0F, 1.0F}, IM_COL32(78, 126, 246, 255), "Z"}};
  draw.AddCircleFilled(center, 5.0F, EditorAccent);
  for (const Axis &axis : Axes) {
    const runtime::Vec2 projected =
        projectSceneDirection3D(camera, axis.worldDirection);
    const float magnitude =
        std::sqrt(projected.x * projected.x + projected.y * projected.y);
    if (magnitude <= 0.05F)
      continue;
    const ImVec2 direction{projected.x / magnitude, projected.y / magnitude};
    const ImVec2 end{center.x + direction.x * 36.0F,
                     center.y + direction.y * 36.0F};
    draw.AddLine(center, end, axis.color, 3.0F);
    draw.AddCircleFilled(end, 5.0F, axis.color);
    draw.AddText(
        {end.x + direction.x * 5.0F - 4.0F, end.y + direction.y * 5.0F - 7.0F},
        axis.color, axis.label);
  }
}

} // namespace

void drawEditorViewport(EditorWorkspace &workspace, const ImVec2 position,
                        const ImVec2 size, EditorViewportArea &viewportArea,
                        std::string &notice) {
  beginEditorPanel("Viewport", position, size,
                   ImGuiWindowFlags_NoScrollbar |
                       ImGuiWindowFlags_NoScrollWithMouse |
                       ImGuiWindowFlags_NoBackground);
  const bool is2D =
      workspace.viewDimension() == EditorSceneViewDimension::TwoDimensional;
  const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float canvasWidth = std::max(available.x, 0.0F);
  const float canvasHeight = std::max(available.y, 0.0F);
  const ImVec2 canvasMax{canvasMin.x + canvasWidth, canvasMin.y + canvasHeight};
  viewportArea = {};
  if (canvasWidth >= 1.0F && canvasHeight >= 1.0F) {
    viewportArea = {
        .x =
            static_cast<std::uint16_t>(std::clamp(canvasMin.x, 0.0F, 65535.0F)),
        .y =
            static_cast<std::uint16_t>(std::clamp(canvasMin.y, 0.0F, 65535.0F)),
        .width =
            static_cast<std::uint16_t>(std::clamp(canvasWidth, 1.0F, 65535.0F)),
        .height = static_cast<std::uint16_t>(
            std::clamp(canvasHeight, 1.0F, 65535.0F))};
  }
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const char *viewLabel =
      is2D ? "2D"
      : workspace.sceneView().projection() == EditorProjection::Perspective
          ? "Perspective"
          : "Orthographic";
  const ImVec2 badgeMin{canvasMin.x + 9.0F, canvasMin.y + 9.0F};
  const ImVec2 badgeText = ImGui::CalcTextSize(viewLabel);
  const ImVec2 badgeMax{badgeMin.x + badgeText.x + 18.0F,
                        badgeMin.y + badgeText.y + 10.0F};
  draw->AddRectFilled(badgeMin, badgeMax, IM_COL32(50, 57, 69, 235), 3.0F);
  draw->AddRect(badgeMin, badgeMax, IM_COL32(80, 88, 103, 255), 3.0F);
  draw->AddText({badgeMin.x + 9.0F, badgeMin.y + 5.0F},
                IM_COL32(224, 227, 235, 255), viewLabel);
  if (is2D) {
    for (const EditorOverlayLine2D &line : buildEditorViewportOverlays2D(
             workspace.project().world, workspace.sceneView2D().camera(),
             {canvasWidth, canvasHeight}, workspace.tilemaps2D(),
             {.grid = workspace.sceneView2D().showGrid,
              .bounds = workspace.sceneView2D().showBounds,
              .cameras = workspace.sceneView2D().showCameras}))
      draw->AddLine({canvasMin.x + line.start.x, canvasMin.y + line.start.y},
                    {canvasMin.x + line.end.x, canvasMin.y + line.end.y},
                    line.rgba, line.width);
  } else {
    const ImVec2 orientation{canvasMax.x - 66.0F, canvasMin.y + 66.0F};
    drawOrientationGizmo(*draw, orientation, workspace.sceneView().camera());
  }
  const runtime::Entity *selected = workspace.selectedEntity();
  const std::string label =
      workspace.selectedIsoGridCell()
          ? "Selected: Cell " +
                isoGridCellKey(workspace.selectedIsoGridCell()->x,
                               workspace.selectedIsoGridCell()->y)
      : selected == nullptr ? "No entity selected"
                            : "Selected: " + selected->name;
  draw->AddText({canvasMin.x + 14.0F, badgeMax.y + 10.0F},
                IM_COL32(205, 209, 218, 255), label.c_str());
  if (canvasWidth >= 1.0F && canvasHeight >= 1.0F) {
    ImGui::InvisibleButton("viewport-canvas", {canvasWidth, canvasHeight});
    const bool hovered = ImGui::IsItemHovered();
    const bool focused = ImGui::IsWindowFocused();
    ImGuiIO &io = ImGui::GetIO();
    if (focused && !io.WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_F, false)) {
      if (is2D)
        (void)workspace.sceneView2D().frameEntity(workspace.project().world,
                                                  workspace.selectedEntityId());
      else
        (void)workspace.sceneView().frameEntity(workspace.project().world,
                                                workspace.selectedEntityId());
    }
    const EditorViewportInput viewportInput{
        .deltaSeconds = io.DeltaTime,
        .mousePosition = {io.MousePos.x - canvasMin.x,
                          io.MousePos.y - canvasMin.y},
        .mouseDelta = {io.MouseDelta.x, io.MouseDelta.y},
        .viewportSize = {canvasWidth, canvasHeight},
        .wheel = hovered ? io.MouseWheel : 0.0F,
        .hovered = hovered,
        .focused = focused && !io.WantTextInput,
        .orbitButton = ImGui::IsMouseDown(ImGuiMouseButton_Left),
        .panButton = ImGui::IsMouseDown(ImGuiMouseButton_Middle),
        .flyButton = ImGui::IsMouseDown(ImGuiMouseButton_Right),
        .orbitModifier = io.KeyAlt,
        .moveForward = ImGui::IsKeyDown(ImGuiKey_W),
        .moveBackward = ImGui::IsKeyDown(ImGuiKey_S),
        .moveLeft = ImGui::IsKeyDown(ImGuiKey_A),
        .moveRight = ImGui::IsKeyDown(ImGuiKey_D),
        .moveUp = ImGui::IsKeyDown(ImGuiKey_E),
        .moveDown = ImGui::IsKeyDown(ImGuiKey_Q),
        .fast = io.KeyShift,
    };
    if (is2D)
      workspace.sceneView2D().update(viewportInput);
    else
      workspace.sceneView().update(viewportInput);
    std::string interactionError;
    const EditorViewportToolInput toolInput{
        .mousePosition = {io.MousePos.x - canvasMin.x,
                          io.MousePos.y - canvasMin.y},
        .mouseDelta = {io.MouseDelta.x, io.MouseDelta.y},
        .viewportSize = {canvasWidth, canvasHeight},
        .hovered = hovered,
        .focused = focused && !io.WantTextInput,
        .leftPressed = ImGui::IsMouseClicked(ImGuiMouseButton_Left),
        .leftDown = ImGui::IsMouseDown(ImGuiMouseButton_Left),
        .leftReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left),
        .navigationModifier = io.KeyAlt,
        .bypassSnapping = io.KeyShift,
        .cancelPressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false)};
    const bool toolUpdated =
        is2D ? workspace.updateViewportTool2D(toolInput, interactionError)
             : workspace.updateViewportTool(toolInput, interactionError);
    if (!toolUpdated)
      notice = std::move(interactionError);

    const EditorGizmoPresentation gizmo =
        is2D ? workspace.gizmoPresentation2D({canvasWidth, canvasHeight})
             : workspace.gizmoPresentation({canvasWidth, canvasHeight});
    const EditorGizmoOperation drawnOperation =
        is2D ? workspace.viewportTool2D().operation()
             : workspace.viewportTool().operation();
    const auto color = [](const EditorGizmoAxis axis) {
      if (axis == EditorGizmoAxis::X)
        return IM_COL32(239, 79, 104, 255);
      if (axis == EditorGizmoAxis::Y)
        return IM_COL32(91, 215, 125, 255);
      return IM_COL32(78, 126, 246, 255);
    };
    for (const EditorGizmoLine &line : gizmo.axes) {
      const ImVec2 start{canvasMin.x + line.start.x,
                         canvasMin.y + line.start.y};
      const ImVec2 end{canvasMin.x + line.end.x, canvasMin.y + line.end.y};
      draw->AddLine(start, end, color(line.axis), 4.0F);
      if (drawnOperation == EditorGizmoOperation::Rotate)
        draw->AddCircle(end, 6.0F, color(line.axis), 16, 3.0F);
      else if (drawnOperation == EditorGizmoOperation::Scale)
        draw->AddRectFilled({end.x - 5.0F, end.y - 5.0F},
                            {end.x + 5.0F, end.y + 5.0F}, color(line.axis));
      else {
        const float dx = end.x - start.x;
        const float dy = end.y - start.y;
        const float length = std::sqrt(dx * dx + dy * dy);
        const float x = length > 0.001F ? dx / length : 1.0F;
        const float y = length > 0.001F ? dy / length : 0.0F;
        const ImVec2 base{end.x - x * 10.0F, end.y - y * 10.0F};
        draw->AddTriangleFilled(end, {base.x - y * 5.0F, base.y + x * 5.0F},
                                {base.x + y * 5.0F, base.y - x * 5.0F},
                                color(line.axis));
      }
    }
    if (hovered)
      ImGui::SetTooltip(
          is2D ? "Click select (repeat to cycle overlaps) | Middle pan | "
                 "Wheel zoom | F frame | Shift bypass snap"
               : "Alt+Left orbit | Middle pan | Wheel zoom | "
                 "Right+WASDQE fly | F frame | Shift bypass snap");
  } else {
    if (is2D)
      workspace.sceneView2D().update({});
    else
      workspace.sceneView().update({});
    const bool dragging = is2D ? workspace.viewportTool2D().isDragging()
                               : workspace.viewportTool().isDragging();
    if (dragging) {
      std::string interactionError;
      const bool cancelled = is2D ? workspace.updateViewportTool2D(
                                        {.focused = false}, interactionError)
                                  : workspace.updateViewportTool(
                                        {.focused = false}, interactionError);
      if (!cancelled)
        notice = std::move(interactionError);
    }
  }
  ImGui::End();
}

} // namespace demi::editor
