#include "editor/EditorViewportPanel.h"

#include "editor/EditorDragDropPayloads.h"
#include "editor/EditorHudCanvas.h"
#include "editor/EditorModuleCatalog.h"
#include "editor/EditorPanelStyle.h"
#include "editor/EditorPrefabPlacement.h"
#include "editor/EditorTerrainPicking.h"
#include "editor/EditorViewportOverlay2D.h"
#include "editor/EditorViewportProjection.h"
#include "editor/EditorWorkspace.h"

#include <bgfx/bgfx.h>
#include <imgui.h>
#include <imgui/imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

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
                        const ImVec2 size, const std::uint16_t textureIndex,
                        EditorViewportArea &viewportArea,
                        EditorHudViewportState &hudState, const bool hudOnly,
                        std::string &notice, const bool embedded) {
  if (!embedded &&
      !beginEditorPanel(hudOnly ? "HUD" : "Viewport", position, size, nullptr,
                        ImGuiWindowFlags_NoScrollbar |
                            ImGuiWindowFlags_NoScrollWithMouse |
                            ImGuiWindowFlags_NoBackground)) {
    viewportArea = {};
    ImGui::End();
    return;
  }
  const bool is2D = hudOnly || workspace.viewDimension() ==
                                   EditorSceneViewDimension::TwoDimensional;
  const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
  const ImVec2 available = ImGui::GetContentRegionAvail();
  const float canvasWidth = std::max(available.x, 0.0F);
  const float canvasHeight = std::max(available.y, 0.0F);
  const ImVec2 canvasMax{canvasMin.x + canvasWidth, canvasMin.y + canvasHeight};
  std::optional<std::filesystem::path> droppedPrefab;
  std::optional<std::filesystem::path> droppedTerrain;
  std::optional<EditorEntityKind> droppedEntityKind;
  std::optional<runtime::Vec2> dropPosition2D;
  std::optional<runtime::Vec3> dropPosition3D;
  bool canvasHovered = false;
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
    if (textureIndex != UINT16_MAX) {
      const bgfx::Caps *caps = bgfx::getCaps();
      const bool flipVertically = caps != nullptr && caps->originBottomLeft;
      ImGui::Image(bgfx::TextureHandle{textureIndex}, available,
                   flipVertically ? ImVec2{0.0F, 1.0F} : ImVec2{0.0F, 0.0F},
                   flipVertically ? ImVec2{1.0F, 0.0F} : ImVec2{1.0F, 1.0F});
    } else {
      ImGui::InvisibleButton("viewport-canvas", {canvasWidth, canvasHeight});
    }
    // Capture the canvas item's hover state before drag/drop and overlays.
    canvasHovered = ImGui::IsItemHovered();
    const bool acceptsPrefabDrop =
        !hudOnly && !workspace.isPrefabDocument() &&
        workspace.activeDocument() == EditorWorkspaceDocument::Scene;
    const bool acceptsEntityCreationDrop =
        !hudOnly && !is2D &&
        workspace.activeDocument() == EditorWorkspaceDocument::Scene;
    if ((acceptsPrefabDrop || acceptsEntityCreationDrop) &&
        ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *payload =
              acceptsPrefabDrop
                  ? ImGui::AcceptDragDropPayload(EditorPrefabSourcePayload)
                  : nullptr;
          payload != nullptr && payload->IsDelivery()) {
        const auto *data = static_cast<const char *>(payload->Data);
        const bool valid = data != nullptr && payload->DataSize > 1 &&
                           data[payload->DataSize - 1] == '\0';
        if (!valid) {
          notice = "The prefab drag payload is invalid.";
        } else {
          droppedPrefab = std::filesystem::path(std::string(
              data, static_cast<std::size_t>(payload->DataSize - 1)));
          const runtime::Vec2 viewportPosition{
              ImGui::GetIO().MousePos.x - canvasMin.x,
              ImGui::GetIO().MousePos.y - canvasMin.y};
          const runtime::Vec2 viewportSize{canvasWidth, canvasHeight};
          if (is2D) {
            dropPosition2D =
                prefabDropWorldPosition2D(workspace.sceneView2D().camera(),
                                          viewportPosition, viewportSize);
          } else {
            dropPosition3D = sceneDropWorldPosition3D(
                workspace.sceneView().camera(), workspace.project().world,
                viewportPosition, viewportSize);
          }
        }
      }
      if (const ImGuiPayload *payload =
              acceptsEntityCreationDrop
                  ? ImGui::AcceptDragDropPayload(EditorEntityCreationPayload)
                  : nullptr;
          payload != nullptr && payload->IsDelivery()) {
        if (is2D || payload->Data == nullptr ||
            payload->DataSize != sizeof(EditorEntityKind)) {
          notice = "The 3D creation drag payload is invalid for this view.";
        } else {
          std::underlying_type_t<EditorEntityKind> value;
          std::memcpy(&value, payload->Data, sizeof(value));
          if (value < static_cast<int>(EditorEntityKind::Cube) ||
              value > static_cast<int>(EditorEntityKind::Plane)) {
            notice = "The 3D creation drag payload is invalid.";
          } else {
            droppedEntityKind = static_cast<EditorEntityKind>(value);
            dropPosition3D = sceneDropWorldPosition3D(
                workspace.sceneView().camera(), workspace.project().world,
                {ImGui::GetIO().MousePos.x - canvasMin.x,
                 ImGui::GetIO().MousePos.y - canvasMin.y},
                {canvasWidth, canvasHeight});
          }
        }
      }
      if (const ImGuiPayload *payload =
              ImGui::AcceptDragDropPayload(EditorTerrainAssetPayload);
          payload != nullptr && payload->IsDelivery()) {
        const auto *data = static_cast<const char *>(payload->Data);
        if (data == nullptr || payload->DataSize < 2 ||
            data[payload->DataSize - 1] != '\0') {
          notice = "The Terrain asset drag payload is invalid.";
        } else if (is2D) {
          notice = "Terrain assets can be placed in a 3D scene.";
        } else {
          droppedTerrain = std::filesystem::path(std::string(
              data, static_cast<std::size_t>(payload->DataSize - 1)));
          const runtime::Vec2 viewportPosition{
              ImGui::GetIO().MousePos.x - canvasMin.x,
              ImGui::GetIO().MousePos.y - canvasMin.y};
          dropPosition3D = prefabDropWorldPosition3D(
              workspace.sceneView().camera(), viewportPosition,
              {canvasWidth, canvasHeight});
          if (!dropPosition3D)
            notice = "The cursor ray does not intersect the 3D ground plane.";
        }
      }
      ImGui::EndDragDropTarget();
    }
    if (hudOnly && workspace.activeDocument() == EditorWorkspaceDocument::Hud &&
        ImGui::BeginDragDropTarget()) {
      if (const ImGuiPayload *payload =
              ImGui::AcceptDragDropPayload(EditorModulePayload);
          payload != nullptr && payload->IsDelivery()) {
        const auto *data = static_cast<const char *>(payload->Data);
        if (data == nullptr || payload->DataSize < 2 ||
            data[payload->DataSize - 1] != '\0') {
          notice = "The module drag payload is invalid.";
        } else {
          const std::string_view id(data, payload->DataSize - 1);
          const auto catalog = editorModules(workspace);
          const EditorModule *module = resolveModule(catalog, id);
          if (!module) {
            notice = "This module is no longer available.";
          } else {
            const auto &hud = workspace.displayedHud();
            const runtime::Vec2 point{
                (ImGui::GetIO().MousePos.x - canvasMin.x) * hud.canvasSize.x /
                    canvasWidth,
                (ImGui::GetIO().MousePos.y - canvasMin.y) * hud.canvasSize.y /
                    canvasHeight};
            const runtime::ui::UiNode *picked =
                pickEditorHudDropParent(hud, point);
            std::string target(workspace.selectedHudNodeId());
            if (picked)
              target = picked->id;
            std::string error;
            notice = workspace.placeHudModule(*module, point, target, error)
                         ? module->title + " added to HUD"
                         : error;
          }
        }
      }
      ImGui::EndDragDropTarget();
    }
  }
  ImDrawList *draw = ImGui::GetWindowDrawList();
  if (is2D && !hudOnly) {
    for (const EditorOverlayLine2D &line : buildEditorViewportOverlays2D(
             workspace.project().world, workspace.sceneView2D().camera(),
             {canvasWidth, canvasHeight}, workspace.tilemaps2D(),
             {.grid = workspace.sceneView2D().showGrid,
              .bounds = workspace.sceneView2D().showBounds,
              .cameras = workspace.sceneView2D().showCameras}))
      draw->AddLine({canvasMin.x + line.start.x, canvasMin.y + line.start.y},
                    {canvasMin.x + line.end.x, canvasMin.y + line.end.y},
                    line.rgba, line.width);
  } else if (!hudOnly) {
    const ImVec2 orientation{canvasMax.x - 66.0F, canvasMin.y + 66.0F};
    drawOrientationGizmo(*draw, orientation, workspace.sceneView().camera());
  }
  if (canvasWidth >= 1.0F && canvasHeight >= 1.0F) {
    const bool hovered = canvasHovered;
    const bool focused = ImGui::IsWindowFocused();
    ImGuiIO &io = ImGui::GetIO();
    const runtime::ui::UiDocument &hud = workspace.displayedHud();
    const float hudScaleX = canvasWidth / std::max(hud.canvasSize.x, 1.0F);
    const float hudScaleY = canvasHeight / std::max(hud.canvasSize.y, 1.0F);
    const runtime::Vec2 hudMouse{
        (io.MousePos.x - canvasMin.x) / std::max(hudScaleX, 0.001F),
        (io.MousePos.y - canvasMin.y) / std::max(hudScaleY, 0.001F)};
    bool hudConsumed = false;
    const runtime::ui::UiNode *selectedHud = workspace.selectedHudNode();
    if (hudOnly && hovered && !io.KeyAlt &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      bool resize = false;
      if (selectedHud != nullptr) {
        const runtime::ui::Rect selectedRect =
            editorHudEditableRect(*selectedHud);
        const runtime::Vec2 handle{selectedRect.x + selectedRect.width,
                                   selectedRect.y + selectedRect.height};
        const float handleRadius =
            10.0F / std::max(std::min(hudScaleX, hudScaleY), 0.001F);
        resize = std::abs(hudMouse.x - handle.x) <= handleRadius &&
                 std::abs(hudMouse.y - handle.y) <= handleRadius;
      }
      const runtime::ui::UiNode *picked =
          resize ? selectedHud : pickEditorHudNode(hud, hudMouse);
      if (picked != nullptr) {
        if (io.KeyCtrl)
          workspace.toggleHudNodeSelection(picked->id);
        else
          workspace.selectHudNode(picked->id);
        hudState.drag = io.KeyCtrl ? EditorHudViewportState::Drag::None
                        : resize   ? EditorHudViewportState::Drag::Resize
                                   : EditorHudViewportState::Drag::Move;
        hudState.nodeId = picked->id;
        hudState.startMouse = hudMouse;
        hudState.startPosition = picked->layout.position;
        hudState.startSize = picked->layout.size;
        hudConsumed = true;
      }
    }
    if (hudState.drag != EditorHudViewportState::Drag::None) {
      hudConsumed = true;
      if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        const runtime::Vec2 delta{hudMouse.x - hudState.startMouse.x,
                                  hudMouse.y - hudState.startMouse.y};
        const bool resize =
            hudState.drag == EditorHudViewportState::Drag::Resize;
        const runtime::Vec2 value =
            resize
                ? runtime::Vec2{std::max(hudState.startSize.x + delta.x, 1.0F),
                                std::max(hudState.startSize.y + delta.y, 1.0F)}
                : runtime::Vec2{hudState.startPosition.x + delta.x,
                                hudState.startPosition.y + delta.y};
        // Selection clicks must not materialize inherited position/size values.
        const float screenDistance =
            std::hypot(delta.x * hudScaleX, delta.y * hudScaleY);
        if (screenDistance >= io.MouseDragThreshold) {
          std::string error;
          notice = workspace.setHudNodeField(
                       hudState.nodeId, resize ? "size" : "position",
                       nlohmann::json::array({value.x, value.y}), error)
                       ? resize ? "HUD element resized" : "HUD element moved"
                       : error;
        } else if (workspace.previewHudAction(hudState.nodeId)) {
          notice = "HUD action previewed; authored defaults are unchanged";
        }
        hudState = {};
      } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        hudState = {};
      }
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
        .orbitButton =
            !hudConsumed && ImGui::IsMouseDown(ImGuiMouseButton_Left),
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
    if (!hudOnly) {
      if (is2D)
        workspace.sceneView2D().update(viewportInput);
      else
        workspace.sceneView().update(viewportInput);
    }
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
        .navigationModifier = io.KeyAlt || hudConsumed || io.WantTextInput,
        .bypassSnapping = io.KeyShift,
        .cancelPressed = ImGui::IsKeyPressed(ImGuiKey_Escape, false)};
    const bool toolUpdated =
        hudOnly ||
        (is2D ? workspace.updateViewportTool2D(toolInput, interactionError)
              : workspace.updateViewportTool(toolInput, interactionError));
    if (!toolUpdated)
      notice = std::move(interactionError);

    const bool hideTransformGizmo =
        hudOnly || (!is2D && workspace.terrainAuthoring().brushActive());
    const EditorGizmoPresentation gizmo =
        hideTransformGizmo ? EditorGizmoPresentation{}
        : is2D ? workspace.gizmoPresentation2D({canvasWidth, canvasHeight})
               : workspace.gizmoPresentation({canvasWidth, canvasHeight});
    if (!hudOnly && !is2D) {
      const auto mask = projectEditorTerrainExclusions(
          workspace.project().world, workspace.terrainAuthoring(),
          workspace.sceneView().camera(), {canvasWidth, canvasHeight});
      for (const auto &sample : mask) {
        const int alpha =
            static_cast<int>(40 + 160 * std::clamp(sample.weight, 0.0F, 1.0F));
        draw->AddCircleFilled(
            {canvasMin.x + sample.position.x, canvasMin.y + sample.position.y},
            3.0F, IM_COL32(230, 100, 90, alpha));
      }
      const auto ring = projectEditorTerrainBrush(
          workspace.project().world, workspace.terrainAuthoring(),
          workspace.sceneView().camera(), {canvasWidth, canvasHeight});
      for (std::size_t index = 1; index < ring.size(); ++index) {
        if (ring[index - 1] && ring[index])
          draw->AddLine(
              {canvasMin.x + ring[index - 1]->x,
               canvasMin.y + ring[index - 1]->y},
              {canvasMin.x + ring[index]->x, canvasMin.y + ring[index]->y},
              IM_COL32(245, 199, 91, 255), 2.0F);
      }
      const auto &authoring = workspace.terrainAuthoring();
      const auto ruleMask = projectEditorTerrainRuleMask(
          workspace.project().world, authoring, workspace.sceneView().camera(),
          {canvasWidth, canvasHeight});
      const auto channel = [](float value) {
        return static_cast<int>(std::clamp(value, 0.0F, 1.0F) * 255.0F);
      };
      for (const auto &sample : ruleMask) {
        const ImVec2 point{canvasMin.x + sample.position.x,
                           canvasMin.y + sample.position.y};
        if (authoring.maskPreview == EditorTerrainMaskPreview::Biome) {
          draw->AddCircleFilled(point, 3.0F,
                                IM_COL32(channel(sample.color.r),
                                         channel(sample.color.g),
                                         channel(sample.color.b), 200));
          continue;
        }
        const float weight = std::clamp(sample.weight, 0.0F, 1.0F);
        draw->AddCircleFilled(point, 3.0F,
                              IM_COL32(channel(0.15F + 0.85F * weight),
                                       channel(0.55F - 0.25F * weight),
                                       channel(0.95F - 0.70F * weight),
                                       static_cast<int>(40 + 160 * weight)));
      }
    }
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
    for (const auto &node : hud.nodes) {
      const auto *hudNode = &node;
      if (!hudOnly || !workspace.isHudNodeSelected(node.id) ||
          node.parent.empty())
        continue;
      runtime::ui::Rect rect = editorHudEditableRect(*hudNode);
      if (hudState.drag != EditorHudViewportState::Drag::None &&
          hudState.nodeId == hudNode->id) {
        const runtime::Vec2 delta{hudMouse.x - hudState.startMouse.x,
                                  hudMouse.y - hudState.startMouse.y};
        if (hudState.drag == EditorHudViewportState::Drag::Move) {
          rect.x += delta.x;
          rect.y += delta.y;
        } else {
          rect.width = std::max(rect.width + delta.x, 1.0F);
          rect.height = std::max(rect.height + delta.y, 1.0F);
        }
      }
      const ImVec2 rectMin{canvasMin.x + rect.x * hudScaleX,
                           canvasMin.y + rect.y * hudScaleY};
      const ImVec2 rectMax{rectMin.x + rect.width * hudScaleX,
                           rectMin.y + rect.height * hudScaleY};
      draw->AddRect(rectMin, rectMax, IM_COL32(180, 147, 255, 255), 1.0F, 2.0F,
                    ImDrawFlags_None);
      if (hudNode->id == workspace.selectedHudNodeId())
        draw->AddRectFilled({rectMax.x - 5.0F, rectMax.y - 5.0F},
                            {rectMax.x + 5.0F, rectMax.y + 5.0F},
                            IM_COL32(180, 147, 255, 255));
      draw->AddText({rectMin.x, rectMin.y - 19.0F},
                    IM_COL32(210, 194, 255, 255), hudNode->id.c_str());
    }
    if (hovered)
      ImGui::SetTooltip(
          hudOnly ? "Click to select | Ctrl+click toggles selection | Drag to "
                    "move | Drag the "
                    "corner handle to resize"
          : is2D  ? "Click select (repeat to cycle overlaps) | Middle pan | "
                    "Wheel zoom | F frame | Shift bypass snap"
                  : "Alt+Left orbit | Middle pan | Wheel zoom | "
                    "Right+WASDQE fly | F frame | Shift bypass snap");
  } else {
    if (is2D)
      workspace.sceneView2D().update({});
    else
      workspace.sceneView().update({});
    const bool dragging = is2D ? workspace.viewportTool2D().isDragging()
                               : workspace.viewportTool().isDragging() ||
                                     workspace.terrainAuthoring().stroking();
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
  // Rebuilding the preview replaces runtime entities and HUD projections. Do
  // it only after this frame has finished consuming pointers into that state.
  if (droppedPrefab && (dropPosition2D || dropPosition3D)) {
    std::string error;
    const bool placed =
        dropPosition2D ? workspace.instantiatePrefab(*droppedPrefab,
                                                     *dropPosition2D, error)
                       : workspace.instantiatePrefab(*droppedPrefab,
                                                     *dropPosition3D, error);
    notice = placed ? "Prefab instance placed" : std::move(error);
  }
  if (droppedEntityKind && dropPosition3D) {
    std::string error;
    notice = workspace.createEntity(error, {}, *droppedEntityKind,
                                    dropPosition3D)
                 ? "3D primitive placed"
                 : std::move(error);
  }
  if (droppedTerrain && dropPosition3D) {
    std::string error;
    notice = workspace.placeTerrainAsset(*droppedTerrain, dropPosition3D, error)
                 ? "Terrain asset placed"
                 : std::move(error);
  }
  if (!embedded)
    ImGui::End();
}

} // namespace demi::editor
