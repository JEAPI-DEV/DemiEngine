#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "editor/EditorPrefabPlacement.h"
#include "editor/EditorViewportProjection.h"
#include "editor/EditorViewportTool.h"
#include "editor/EditorWorkspace.h"

#include "demi/runtime/scene/components/3dcomponents/Camera3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/terrain/TerrainMeshBuilder.h"

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

float dragAtDistance(float distance, float meshSize,
                     demi::editor::EditorGizmoOperation operation,
                     demi::editor::EditorProjection projection) {
  using namespace demi;
  runtime::World world;
  auto camera = cameraEntity();
  camera.component<runtime::Transform3DComponent>()->position.z = -distance;
  world.entities.push_back(std::move(camera));
  auto object = cube("cube", 0);
  object.component<runtime::MeshRendererComponent>()->size = {
      meshSize, meshSize, meshSize};
  world.entities.push_back(std::move(object));
  editor::EditorSceneViewState view;
  view.reset(world);
  assert(view.alignToFirstCamera(world));
  view.setProjection(projection);
  editor::EditorViewportTool tool;
  tool.setOperation(operation);
  const runtime::Vec2 viewport{800, 600};
  const auto presentation = tool.presentation(world, "cube", view, viewport);
  const auto axis =
      std::ranges::find(presentation.axes, editor::EditorGizmoAxis::X,
                        &editor::EditorGizmoLine::axis);
  assert(axis != presentation.axes.end());
  tool.update(world, "cube", view,
              {.mousePosition = midpoint(*axis),
               .viewportSize = viewport,
               .hovered = true,
               .focused = true,
               .leftPressed = true,
               .leftDown = true});
  assert(tool.isDragging());
  const auto action = tool.update(world, "cube", view,
                                  {.mouseDelta = direction(*axis, 40),
                                   .viewportSize = viewport,
                                   .hovered = true,
                                   .focused = true,
                                   .leftDown = true,
                                   .bypassSnapping = true});
  assert(action.edit);
  return action.edit->value[0].get<float>() -
         (operation == editor::EditorGizmoOperation::Scale ? 1 : 0);
}
} // namespace

int main() {
  using namespace demi;
  for (const auto operation : {editor::EditorGizmoOperation::Translate,
                               editor::EditorGizmoOperation::Scale}) {
    const float near =
        dragAtDistance(10, 1, operation, editor::EditorProjection::Perspective);
    const float far = dragAtDistance(1000, 1, operation,
                                     editor::EditorProjection::Perspective);
    assert(std::abs(far / near - 100) < .01F);
    const float orthoNear = dragAtDistance(
        10, 1, operation, editor::EditorProjection::Orthographic);
    const float orthoFar = dragAtDistance(
        1000, 1, operation, editor::EditorProjection::Orthographic);
    assert(std::abs(orthoFar - orthoNear) < .001F);
  }
  const float unitScale =
      dragAtDistance(100, 1, editor::EditorGizmoOperation::Scale,
                     editor::EditorProjection::Perspective);
  const float largeScale =
      dragAtDistance(100, 10, editor::EditorGizmoOperation::Scale,
                     editor::EditorProjection::Perspective);
  assert(std::abs(unitScale / largeScale - 10) < .01F);
  {
    runtime::World closeWorld;
    closeWorld.entities.push_back(cameraEntity());
    closeWorld.entities.push_back(cube("close-cube", 0));
    editor::EditorSceneViewCamera closeCamera;
    closeCamera.position = {0, 0, -.45F};
    closeCamera.forward = {0, 0, 1};
    closeCamera.projection.nearClip = .1F;
    const runtime::Vec2 viewport{800, 600};
    assert(editor::pickSceneEntity3D(closeWorld, closeCamera, {400, 300},
                                     viewport) == "close-cube");
    closeCamera.position.z = -.51F;
    assert(editor::pickSceneEntity3D(closeWorld, closeCamera, {400, 300},
                                     viewport) == "close-cube");
    closeWorld.entities.back()
        .component<runtime::MeshRendererComponent>()
        ->size = {.02F, .02F, .02F};
    closeWorld.entities.back()
        .component<runtime::Transform3DComponent>()
        ->position.z = -.48F;
    assert(!editor::pickSceneEntity3D(closeWorld, closeCamera, {400, 300},
                                      viewport));
    closeCamera.position.z = -.45F;
    closeWorld.entities.back() = cube("close-cube", 0);
    closeWorld.entities.front()
        .component<runtime::Transform3DComponent>()
        ->position = closeCamera.position;
    assert(editor::pickSceneEntity3D(closeWorld, closeCamera, {400, 300},
                                     viewport) == "close-cube");
    editor::EditorSceneViewState view;
    view.reset(closeWorld);
    assert(view.alignToFirstCamera(closeWorld));
    editor::EditorViewportTool tool;
    const auto handles =
        tool.presentation(closeWorld, "close-cube", view, viewport);
    const auto axis =
        std::ranges::find(handles.axes, editor::EditorGizmoAxis::X,
                          &editor::EditorGizmoLine::axis);
    assert(axis != handles.axes.end());
    assert(std::abs(std::hypot(axis->end.x - axis->start.x,
                               axis->end.y - axis->start.y) -
                    72) < .01F);
  }
  runtime::World world;
  world.entities.push_back(cameraEntity());
  world.entities.push_back(cube("cube", 0.0F));
  world.entities.push_back(cube("duplicate", 3.0F));
  editor::EditorSceneViewState sceneView;
  sceneView.reset(world);
  const auto dropPosition = editor::prefabDropWorldPosition3D(
      sceneView.camera(), {400.0F, 300.0F}, {800.0F, 600.0F});
  assert(dropPosition.has_value());
  assert(std::abs(dropPosition->y) < 0.001F);
  // Picking/gizmo assertions below use the fixture camera, not the overview.
  assert(sceneView.alignToFirstCamera(world));
  const runtime::Vec2 viewport{800.0F, 600.0F};

  {
    runtime::World instanceWorld;
    auto owner = cube("instances", 20.0F);
    runtime::MeshInstances3DComponent instances;
    instances.transforms["center"].position = {-20.0F, 0.0F, 0.0F};
    owner.setComponent(instances);
    instanceWorld.entities.push_back(owner);
    assert(editor::pickSceneEntity3D(instanceWorld, sceneView.camera(),
                                     {400.0F, 300.0F},
                                     viewport) == "instances");
    instanceWorld.entities.front()
        .component<runtime::MeshInstances3DComponent>()
        ->transforms.clear();
    assert(!editor::pickSceneEntity3D(instanceWorld, sceneView.camera(),
                                      {400.0F, 300.0F}, viewport));
  }

  assert(editor::pickSceneEntity3D(world, sceneView.camera(), {400.0F, 300.0F},
                                   viewport) == "cube");
  const auto duplicateScreen = editor::projectScenePoint3D(
      sceneView.camera(), {3.0F, 0.0F, 0.0F}, viewport);
  assert(duplicateScreen.has_value());
  assert(duplicateScreen->x < 400.0F);
  assert(editor::projectSceneDirection3D(sceneView.camera(), {1.0F, 0.0F, 0.0F})
             .x < 0.0F);
  assert(editor::pickSceneEntity3D(world, sceneView.camera(), *duplicateScreen,
                                   viewport) == "duplicate");
  std::erase_if(world.entities,
                [](const auto &entity) { return entity.id == "cube"; });
  assert(!editor::pickSceneEntity3D(world, sceneView.camera(), {400.0F, 300.0F},
                                    viewport));
  world.entities.push_back(cube("reloaded-cube", 0.0F));
  assert(editor::pickSceneEntity3D(world, sceneView.camera(), {400.0F, 300.0F},
                                   viewport) == "reloaded-cube");

  // A generated surface selects its authored terrain placement.
  {
    runtime::World terrainWorld;
    terrainWorld.entities.push_back(cube("terrain", 20.0F));
    auto surface = cube("generated-chunk", 0.0F);
    surface.setComponent(
        runtime::terrain_detail::TerrainGeneratedSurface{.owner = "terrain"});
    terrainWorld.entities.push_back(std::move(surface));
    editor::EditorViewportTool selectionTool;
    const auto picked = selectionTool.update(terrainWorld, "", sceneView,
                                             {.mousePosition = {400.0F, 300.0F},
                                              .viewportSize = viewport,
                                              .hovered = true,
                                              .leftPressed = true,
                                              .leftDown = true});
    assert(picked.selectionChanged && picked.selectedEntityId == "terrain");
  }

  // One press owns one gizmo target, even when the cursor crosses another
  // selectable entity or leaves the viewport before release.
  for (const auto operation : {editor::EditorGizmoOperation::Translate,
                               editor::EditorGizmoOperation::Rotate,
                               editor::EditorGizmoOperation::Scale}) {
    editor::EditorViewportTool dragTool;
    dragTool.setOperation(operation);
    const auto lines =
        dragTool.presentation(world, "reloaded-cube", sceneView, viewport);
    const runtime::Vec2 start = midpoint(lines.axes.front());
    const auto press = dragTool.update(world, "reloaded-cube", sceneView,
                                       {.mousePosition = start,
                                        .viewportSize = viewport,
                                        .hovered = true,
                                        .leftPressed = true,
                                        .leftDown = true});
    assert(dragTool.isDragging() && !press.selectionChanged);
    const auto crossed =
        dragTool.update(world, "reloaded-cube", sceneView,
                        {.mousePosition = *duplicateScreen,
                         .mouseDelta = direction(lines.axes.front(), 35.0F),
                         .viewportSize = viewport,
                         .hovered = true,
                         .focused = true,
                         .leftDown = true,
                         .bypassSnapping = true});
    assert(crossed.edit && crossed.edit->target.entityId == "reloaded-cube" &&
           !crossed.selectionChanged);
    const auto outside = dragTool.update(
        world, "reloaded-cube", sceneView,
        {.mousePosition = {viewport.x + 20.0F, viewport.y + 20.0F},
         .mouseDelta = direction(lines.axes.front(), 10.0F),
         .viewportSize = viewport,
         .focused = true,
         .leftDown = true,
         .bypassSnapping = true});
    assert(outside.edit && outside.edit->target.entityId == "reloaded-cube" &&
           !outside.selectionChanged && dragTool.isDragging());
    const auto released = dragTool.update(
        world, "reloaded-cube", sceneView,
        {.viewportSize = viewport, .focused = true, .leftReleased = true});
    assert(released.completion == editor::EditorDragCompletion::Finish &&
           !released.selectionChanged && !dragTool.isDragging());
    const auto nextPress = dragTool.update(world, "reloaded-cube", sceneView,
                                           {.mousePosition = *duplicateScreen,
                                            .viewportSize = viewport,
                                            .hovered = true,
                                            .leftPressed = true,
                                            .leftDown = true});
    assert(nextPress.selectionChanged &&
           nextPress.selectedEntityId == "duplicate");

    const auto restart = dragTool.update(world, "reloaded-cube", sceneView,
                                         {.mousePosition = start,
                                          .viewportSize = viewport,
                                          .hovered = true,
                                          .leftPressed = true,
                                          .leftDown = true});
    assert(!restart.selectionChanged && dragTool.isDragging());
    const auto cancelled = dragTool.update(
        world, "reloaded-cube", sceneView,
        {.viewportSize = viewport, .leftDown = true, .cancelPressed = true});
    assert(cancelled.completion == editor::EditorDragCompletion::Cancel &&
           !cancelled.selectionChanged && !dragTool.isDragging());
  }

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
  const float xAxisPixels =
      std::hypot(xAxis->end.x - xAxis->start.x, xAxis->end.y - xAxis->start.y);
  const float translationPerPixel = xAxis->worldLength / xAxisPixels;
  assert(action.edit->value[0] ==
         std::round(120.0F * translationPerPixel / sceneView.translationSnap) *
             sceneView.translationSnap);
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
  assert(std::abs(action.edit->value.at(0).get<float>() -
                  37.0F * translationPerPixel) < 0.001F);
  action = tool.update(
      world, "reloaded-cube", sceneView,
      {.viewportSize = viewport, .focused = true, .leftReleased = true});
  assert(action.completion == editor::EditorDragCompletion::Finish);

  tool.setOperation(editor::EditorGizmoOperation::Rotate);
  const auto rotateGizmo =
      tool.presentation(world, "reloaded-cube", sceneView, viewport);
  const runtime::Vec2 rotateHandle = midpoint(rotateGizmo.axes.front());
  assert(rotateGizmo.axes.size() == 288);
  const auto rotateEnd = midpoint(rotateGizmo.axes.at(24));
  const runtime::Vec2 rotateDelta{rotateEnd.x - rotateHandle.x, rotateEnd.y - rotateHandle.y};
  action = tool.update(world, "reloaded-cube", sceneView,
                       {.mousePosition = rotateHandle,
                        .viewportSize = viewport,
                        .hovered = true,
                        .focused = true,
                        .leftPressed = true,
                        .leftDown = true});
  assert(tool.isDragging());
  action = tool.update(world, "reloaded-cube", sceneView,
                       {.mousePosition = rotateEnd,
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
  assert(action.completion == editor::EditorDragCompletion::None &&
         tool.isDragging());
  action = tool.update(
      world, "reloaded-cube", sceneView,
      {.viewportSize = viewport, .focused = false, .leftDown = true});
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
