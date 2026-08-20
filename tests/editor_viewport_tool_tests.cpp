#include "editor/EditorViewportProjection.h"
#include "editor/EditorViewportTool.h"
#include "editor/EditorWorkspace.h"

#include "demi/runtime/scene/components/3dcomponents/Camera3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <ranges>
#include <string>

namespace {

demi::runtime::Entity cameraEntity() {
  demi::runtime::Entity entity;
  entity.id = "camera";
  entity.setComponent(
      demi::runtime::Transform3DComponent{.position = {0.0F, 0.0F, -10.0F}});
  entity.setComponent(
      demi::runtime::Camera3DComponent{.targetOffset = {0.0F, 0.0F, 1.0F}});
  return entity;
}

demi::runtime::Entity cube(std::string id, const float x) {
  demi::runtime::Entity entity;
  entity.id = std::move(id);
  entity.setComponent(
      demi::runtime::Transform3DComponent{.position = {x, 0.0F, 0.0F}});
  entity.setComponent(demi::runtime::MeshRendererComponent{
      .shape = "cube", .size = {1.0F, 1.0F, 1.0F}});
  return entity;
}

demi::runtime::Vec2 midpoint(const demi::editor::EditorGizmoLine &line) {
  return {(line.start.x + line.end.x) * 0.5F,
          (line.start.y + line.end.y) * 0.5F};
}

demi::runtime::Vec2 direction(const demi::editor::EditorGizmoLine &line,
                              const float pixels) {
  const float x = line.end.x - line.start.x;
  const float y = line.end.y - line.start.y;
  const float magnitude = std::sqrt(x * x + y * y);
  assert(magnitude > 0.001F);
  return {x * pixels / magnitude, y * pixels / magnitude};
}

} // namespace

int main() {
  using namespace demi;
  runtime::World world;
  world.entities.push_back(cameraEntity());
  world.entities.push_back(cube("cube", 0.0F));
  world.entities.push_back(cube("duplicate", 3.0F));
  editor::EditorSceneViewState sceneView;
  sceneView.reset(world);
  const runtime::Vec2 viewport{800.0F, 600.0F};

  assert(editor::pickSceneEntity3D(world, sceneView.camera(), {400.0F, 300.0F},
                                   viewport) == "cube");
  const auto duplicateScreen = editor::projectScenePoint3D(
      sceneView.camera(), {3.0F, 0.0F, 0.0F}, viewport);
  assert(duplicateScreen.has_value());
  assert(duplicateScreen->x < 400.0F);
  assert(editor::projectSceneDirection3D(sceneView.camera(),
                                         {1.0F, 0.0F, 0.0F})
             .x < 0.0F);
  assert(editor::pickSceneEntity3D(world, sceneView.camera(),
                                   *duplicateScreen, viewport) == "duplicate");
  std::erase_if(world.entities,
                [](const auto &entity) { return entity.id == "cube"; });
  assert(!editor::pickSceneEntity3D(world, sceneView.camera(), {400.0F, 300.0F},
                                    viewport));
  world.entities.push_back(cube("reloaded-cube", 0.0F));
  assert(editor::pickSceneEntity3D(world, sceneView.camera(), {400.0F, 300.0F},
                                   viewport) == "reloaded-cube");

  editor::EditorViewportTool tool;
  const auto gizmo =
      tool.presentation(world, "reloaded-cube", sceneView, viewport);
  assert(gizmo.axes.size() == 3);
  const auto xAxis = std::ranges::find(gizmo.axes, editor::EditorGizmoAxis::X,
                                       &editor::EditorGizmoLine::axis);
  assert(xAxis != gizmo.axes.end());
  const runtime::Vec2 handle = midpoint(*xAxis);
  const runtime::Vec2 xDrag = direction(*xAxis, 120.0F);
  auto action = tool.update(world, "reloaded-cube", sceneView,
                            {.mousePosition = handle,
                             .viewportSize = viewport,
                             .hovered = true,
                             .focused = true,
                             .leftPressed = true,
                             .leftDown = true});
  assert(!action.edit && tool.isDragging());
  action = tool.update(world, "reloaded-cube", sceneView,
                       {.mousePosition = handle,
                        .mouseDelta = xDrag,
                        .viewportSize = viewport,
                        .hovered = true,
                        .focused = true,
                        .leftDown = true});
  assert(action.edit.has_value());
  assert(action.edit->target.entityId == "reloaded-cube");
  assert(action.edit->target.field == "position");
  assert(action.edit->value[0] == 1.0F);
  action = tool.update(
      world, "reloaded-cube", sceneView,
      {.viewportSize = viewport, .focused = true, .leftReleased = true});
  assert(action.completion == editor::EditorDragCompletion::Finish);

  // Holding Shift temporarily bypasses the configured translation grid.
  action = tool.update(world, "reloaded-cube", sceneView,
                       {.mousePosition = handle,
                        .viewportSize = viewport,
                        .hovered = true,
                        .focused = true,
                        .leftPressed = true,
                        .leftDown = true});
  assert(tool.isDragging());
  action = tool.update(world, "reloaded-cube", sceneView,
                       {.mousePosition = handle,
                        .mouseDelta = direction(*xAxis, 37.0F),
                        .viewportSize = viewport,
                        .hovered = true,
                        .focused = true,
                        .leftDown = true,
                        .bypassSnapping = true});
  assert(action.edit.has_value());
  assert(std::abs(action.edit->value.at(0).get<float>() - 0.37F) < 0.001F);
  action = tool.update(
      world, "reloaded-cube", sceneView,
      {.viewportSize = viewport, .focused = true, .leftReleased = true});
  assert(action.completion == editor::EditorDragCompletion::Finish);

  tool.setOperation(editor::EditorGizmoOperation::Rotate);
  const auto rotateGizmo =
      tool.presentation(world, "reloaded-cube", sceneView, viewport);
  const runtime::Vec2 rotateHandle = midpoint(rotateGizmo.axes.front());
  const runtime::Vec2 rotateDelta{
      rotateGizmo.axes.front().end.x - rotateGizmo.axes.front().start.x,
      rotateGizmo.axes.front().end.y - rotateGizmo.axes.front().start.y};
  action = tool.update(world, "reloaded-cube", sceneView,
                       {.mousePosition = rotateHandle,
                        .viewportSize = viewport,
                        .hovered = true,
                        .focused = true,
                        .leftPressed = true,
                        .leftDown = true});
  assert(tool.isDragging());
  action = tool.update(world, "reloaded-cube", sceneView,
                       {.mousePosition = rotateHandle,
                        .mouseDelta = rotateDelta,
                        .viewportSize = viewport,
                        .hovered = true,
                        .focused = true,
                        .leftDown = true});
  assert(action.edit && action.edit->target.field == "rotation");
  constexpr float RotationSnapRadians = 0.2617993878F;
  const float rotation = action.edit->value.at(0).get<float>();
  assert(std::abs(rotation) > 0.001F);
  assert(std::abs(rotation / RotationSnapRadians -
                  std::round(rotation / RotationSnapRadians)) < 0.001F);
  action = tool.update(
      world, "reloaded-cube", sceneView,
      {.viewportSize = viewport, .focused = false, .leftDown = true});
  assert(action.completion == editor::EditorDragCompletion::Cancel);
  assert(!tool.isDragging());

  tool.setOperation(editor::EditorGizmoOperation::Scale);
  const auto scaleGizmo =
      tool.presentation(world, "reloaded-cube", sceneView, viewport);
  const runtime::Vec2 scaleHandle = midpoint(scaleGizmo.axes.front());
  action = tool.update(world, "reloaded-cube", sceneView,
                       {.mousePosition = scaleHandle,
                        .viewportSize = viewport,
                        .hovered = true,
                        .focused = true,
                        .leftPressed = true,
                        .leftDown = true});
  assert(tool.isDragging());
  const runtime::Vec2 scaleDelta{
      scaleGizmo.axes.front().end.x - scaleGizmo.axes.front().start.x,
      scaleGizmo.axes.front().end.y - scaleGizmo.axes.front().start.y};
  action = tool.update(world, "reloaded-cube", sceneView,
                       {.mousePosition = scaleHandle,
                        .mouseDelta = scaleDelta,
                        .viewportSize = viewport,
                        .hovered = true,
                        .focused = true,
                        .leftDown = true});
  assert(action.edit && action.edit->target.field == "scale");
  const float scale = action.edit->value.at(0).get<float>();
  assert(scale > 1.0F);
  assert(std::abs((scale - 1.0F) / sceneView.scaleSnap -
                  std::round((scale - 1.0F) / sceneView.scaleSnap)) < 0.001F);
  action = tool.update(world, "reloaded-cube", sceneView,
                       {.viewportSize = viewport,
                        .hovered = true,
                        .focused = false,
                        .leftDown = true});
  assert(action.completion == editor::EditorDragCompletion::Cancel);

  action = tool.update(world, "reloaded-cube", sceneView,
                       {.mousePosition = scaleHandle,
                        .viewportSize = viewport,
                        .hovered = true,
                        .focused = true,
                        .leftPressed = true,
                        .leftDown = true});
  assert(tool.isDragging());
  std::erase_if(world.entities, [](const auto &entity) {
    return entity.id == "reloaded-cube";
  });
  action = tool.update(world, "reloaded-cube", sceneView,
                       {.viewportSize = viewport,
                        .hovered = true,
                        .focused = true,
                        .leftDown = true});
  assert(action.completion == editor::EditorDragCompletion::Cancel);
  assert(!tool.isDragging());

  action = tool.update(world, "", sceneView,
                       {.mousePosition = {20.0F, 20.0F},
                        .viewportSize = viewport,
                        .hovered = true,
                        .focused = true,
                        .leftPressed = true,
                        .leftDown = true});
  assert(action.selectionChanged && action.selectedEntityId.empty());

  editor::EditorWorkspace workspace;
  std::string error;
  assert(workspace.open(
      std::filesystem::path(DEMI_SOURCE_DIR) / "examples/minimal_3d", error));
  workspace.selectEntity("");
  assert(workspace.refresh(error));
  assert(workspace.selectedEntityId().empty());
  const auto transformEntity = std::ranges::find_if(
      workspace.project().world.entities, [](const auto &entity) {
        return entity.enabled &&
               entity.template hasComponent<runtime::Transform3DComponent>() &&
               !entity.template hasComponent<runtime::Camera3DComponent>();
      });
  assert(transformEntity != workspace.project().world.entities.end());
  workspace.selectEntity(transformEntity->id);
  assert(workspace.sceneView().frameEntity(workspace.project().world,
                                           transformEntity->id));
  const runtime::Vec2 editorViewport{1200.0F, 700.0F};
  const auto editorGizmo = workspace.gizmoPresentation(editorViewport);
  assert(!editorGizmo.axes.empty());
  const runtime::Vec2 editorHandle = midpoint(editorGizmo.axes.front());
  const runtime::Vec2 editorDrag{
      (editorGizmo.axes.front().end.x - editorGizmo.axes.front().start.x) *
          2.0F,
      (editorGizmo.axes.front().end.y - editorGizmo.axes.front().start.y) *
          2.0F};
  const std::string canonical = workspace.sceneDocument().json().dump();
  assert(workspace.updateViewportTool({.mousePosition = editorHandle,
                                       .viewportSize = editorViewport,
                                       .hovered = true,
                                       .focused = true,
                                       .leftPressed = true,
                                       .leftDown = true},
                                      error));
  assert(workspace.updateViewportTool({.mousePosition = editorHandle,
                                       .mouseDelta = editorDrag,
                                       .viewportSize = editorViewport,
                                       .hovered = true,
                                       .focused = true,
                                       .leftDown = true},
                                      error));
  assert(workspace.updateViewportTool({.mousePosition = editorHandle,
                                       .mouseDelta = editorDrag,
                                       .viewportSize = editorViewport,
                                       .hovered = true,
                                       .focused = true,
                                       .leftDown = true},
                                      error));
  assert(workspace.sceneDocument().json().dump() != canonical);
  assert(workspace.updateViewportTool(
      {.viewportSize = editorViewport, .focused = true, .leftReleased = true},
      error));
  assert(workspace.undo(error));
  assert(workspace.sceneDocument().json().dump() == canonical);

  const auto cancelGizmo = workspace.gizmoPresentation(editorViewport);
  const runtime::Vec2 cancelHandle = midpoint(cancelGizmo.axes.front());
  const runtime::Vec2 cancelDrag{
      (cancelGizmo.axes.front().end.x - cancelGizmo.axes.front().start.x) *
          2.0F,
      (cancelGizmo.axes.front().end.y - cancelGizmo.axes.front().start.y) *
          2.0F};
  assert(workspace.updateViewportTool({.mousePosition = cancelHandle,
                                       .viewportSize = editorViewport,
                                       .hovered = true,
                                       .focused = true,
                                       .leftPressed = true,
                                       .leftDown = true},
                                      error));
  assert(workspace.updateViewportTool({.mousePosition = cancelHandle,
                                       .mouseDelta = cancelDrag,
                                       .viewportSize = editorViewport,
                                       .hovered = true,
                                       .focused = true,
                                       .leftDown = true},
                                      error));
  assert(workspace.updateViewportTool({.viewportSize = editorViewport,
                                       .focused = true,
                                       .leftDown = true,
                                       .cancelPressed = true},
                                      error));
  assert(workspace.sceneDocument().json().dump() == canonical);
  assert(!workspace.viewportTool().isDragging());
  return 0;
}
