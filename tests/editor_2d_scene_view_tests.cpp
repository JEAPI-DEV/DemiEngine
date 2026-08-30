#include "editor/EditorIsoGridCell.h"
#include "editor/EditorIsoGridCellDocument.h"
#include "editor/EditorIsoScene2D.h"
#include "editor/EditorSceneDomain.h"
#include "editor/EditorViewportOverlay2D.h"
#include "editor/EditorViewportProjection2D.h"
#include "editor/EditorWorkspace.h"

#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/2dcomponents/Camera2DComponent.h"
#include "demi/runtime/scene/components/2dcomponents/IsoGridComponent.h"
#include "demi/runtime/scene/components/2dcomponents/IsoTransformComponent.h"
#include "demi/runtime/scene/components/2dcomponents/SpriteComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <ranges>
#include <string>
#include <utility>

namespace {

bool close(const float left, const float right) {
  return std::abs(left - right) < 0.001F;
}

demi::runtime::Entity sprite(std::string id, const float x) {
  demi::runtime::Entity entity;
  entity.id = std::move(id);
  entity.setComponent(
      demi::runtime::Transform2DComponent{.position = {x, 0.0F}});
  entity.setComponent(demi::runtime::SpriteComponent{.size = {2.0F, 2.0F}});
  return entity;
}

demi::runtime::Vec2 midpoint(const demi::editor::EditorGizmoLine &line) {
  return {(line.start.x + line.end.x) * 0.5F,
          (line.start.y + line.end.y) * 0.5F};
}

} // namespace

int main() {
  using namespace demi;
  runtime::World world;
  world.entities.push_back(sprite("front", 0.0F));
  assert(editor::detectEditorSceneDomain(world) ==
         editor::EditorSceneDomain::TwoDimensional);
  runtime::Entity threeDimensional;
  threeDimensional.id = "three-dimensional";
  threeDimensional.setComponent(runtime::Transform3DComponent{});
  world.entities.push_back(std::move(threeDimensional));
  assert(editor::detectEditorSceneDomain(world) ==
         editor::EditorSceneDomain::Mixed);
  world.entities.pop_back();

  editor::EditorSceneView2DState sceneView;
  sceneView.reset(world);
  const runtime::Vec2 viewport{800.0F, 600.0F};
  const editor::EditorSceneView2DCamera initial = sceneView.camera();
  sceneView.update({.mouseDelta = {30.0F, -15.0F},
                    .viewportSize = viewport,
                    .hovered = true,
                    .focused = true,
                    .panButton = true});
  assert(sceneView.capturesPointer());
  assert(!close(sceneView.camera().position.x, initial.position.x));
  sceneView.update({.viewportSize = viewport,
                    .wheel = 1.0F,
                    .hovered = true,
                    .focused = true});
  assert(sceneView.camera().projection.orthographicSize <
         initial.projection.orthographicSize);
  const runtime::Vec2 projected =
      editor::projectScenePoint2D(sceneView.camera(), {3.0F, -2.0F}, viewport);
  const runtime::Vec2 unprojected =
      editor::unprojectScenePoint2D(sceneView.camera(), projected, viewport);
  assert(close(unprojected.x, 3.0F) && close(unprojected.y, -2.0F));

  sceneView.reset(world);
  assert(editor::pickSceneEntity2D(world, sceneView.camera(), {400.0F, 300.0F},
                                   viewport) == "front");
  runtime::World overlapWorld;
  overlapWorld.entities.push_back(sprite("behind", 0.0F));
  overlapWorld.entities.push_back(sprite("front", 0.0F));
  assert(editor::pickSceneEntity2D(overlapWorld, sceneView.camera(),
                                   {400.0F, 300.0F}, viewport) == "front");
  assert(editor::pickSceneEntity2D(overlapWorld, sceneView.camera(),
                                   {400.0F, 300.0F}, viewport,
                                   "front") == "behind");
  const auto overlays = editor::buildEditorViewportOverlays2D(
      world, sceneView.camera(), viewport, {},
      {.grid = true, .bounds = true, .cameras = true});
  assert(overlays.size() >= 8);

  runtime::World isoWorld;
  runtime::Entity grid;
  grid.id = "grid";
  grid.setComponent(runtime::IsoGridComponent{
      .cellSize = {1.0F, 0.5F}, .width = 8, .height = 8});
  runtime::Entity road;
  road.id = "road";
  road.setComponent(runtime::IsoTransformComponent{.tile = {2.0F, 3.0F},
                                                   .footprint = {1.0F, 1.0F}});
  road.setComponent(
      runtime::SpriteComponent{.size = {2.0F, 1.0F}, .pivot = {0.5F, 1.0F}});
  isoWorld.entities.push_back(std::move(grid));
  isoWorld.entities.push_back(std::move(road));
  sceneView.reset(isoWorld);
  const auto roadVisual = editor::editorIsoVisual2D(
      isoWorld, isoWorld.entities.back(), sceneView.camera(), viewport);
  assert(roadVisual);
  const runtime::Vec2 roadPoint{
      (roadVisual->screenMinimum.x + roadVisual->screenMaximum.x) * 0.5F,
      (roadVisual->screenMinimum.y + roadVisual->screenMaximum.y) * 0.5F};
  assert(editor::pickSceneEntity2D(isoWorld, sceneView.camera(), roadPoint,
                                   viewport) == "road");
  assert(sceneView.frameEntity(isoWorld, "road"));
  editor::EditorViewportTool2D isoTool;
  const auto isoGizmo =
      isoTool.presentation(isoWorld, "road", sceneView, viewport);
  assert(isoGizmo.axes.size() == 2);
  const runtime::Vec2 isoHandle = midpoint(isoGizmo.axes.front());
  assert(!isoTool
              .update(isoWorld, "road", sceneView,
                      {.mousePosition = isoHandle,
                       .viewportSize = viewport,
                       .hovered = true,
                       .focused = true,
                       .leftPressed = true,
                       .leftDown = true})
              .edit);
  const runtime::Vec2 isoDelta{
      isoGizmo.axes.front().end.x - isoGizmo.axes.front().start.x,
      isoGizmo.axes.front().end.y - isoGizmo.axes.front().start.y};
  const auto isoEdit = isoTool.update(isoWorld, "road", sceneView,
                                      {.mousePosition = isoHandle,
                                       .mouseDelta = isoDelta,
                                       .viewportSize = viewport,
                                       .hovered = true,
                                       .focused = true,
                                       .leftDown = true,
                                       .bypassSnapping = true});
  assert(isoEdit.edit && isoEdit.edit->target.component == "IsoTransform" &&
         isoEdit.edit->target.field == "tile");
  assert(close(isoEdit.edit->value.at(0).get<float>(), 3.0F));
  assert(close(isoEdit.edit->value.at(1).get<float>(), 3.0F));

  editor::EditorViewportTool2D tool;
  const auto gizmo = tool.presentation(world, "front", sceneView, viewport);
  assert(gizmo.axes.size() == 2);
  const auto xAxis = std::ranges::find(gizmo.axes, editor::EditorGizmoAxis::X,
                                       &editor::EditorGizmoLine::axis);
  assert(xAxis != gizmo.axes.end());
  const runtime::Vec2 handle = midpoint(*xAxis);
  auto action = tool.update(world, "front", sceneView,
                            {.mousePosition = handle,
                             .viewportSize = viewport,
                             .hovered = true,
                             .focused = true,
                             .leftPressed = true,
                             .leftDown = true});
  assert(tool.isDragging() && !action.edit);
  action = tool.update(world, "front", sceneView,
                       {.mousePosition = handle,
                        .mouseDelta = {72.0F, 0.0F},
                        .viewportSize = viewport,
                        .hovered = true,
                        .focused = true,
                        .leftDown = true,
                        .bypassSnapping = true});
  assert(action.edit && action.edit->target.component == "Transform2D" &&
         action.edit->target.field == "position");

  runtime::World parentedWorld;
  runtime::Entity parent;
  parent.id = "parent";
  parent.setComponent(runtime::Transform2DComponent{
      .position = {4.0F, 2.0F}, .rotation = 0.5F, .scale = {2.0F, 3.0F}});
  runtime::Entity child = sprite("child", 1.0F);
  child.component<runtime::Transform2DComponent>()->parent = "parent";
  parentedWorld.entities.push_back(std::move(parent));
  parentedWorld.entities.push_back(std::move(child));
  sceneView.reset(parentedWorld);
  assert(sceneView.frameEntity(parentedWorld, "child"));
  editor::EditorViewportTool2D parentedTool;
  const auto parentedGizmo =
      parentedTool.presentation(parentedWorld, "child", sceneView, viewport);
  const auto parentedX =
      std::ranges::find(parentedGizmo.axes, editor::EditorGizmoAxis::X,
                        &editor::EditorGizmoLine::axis);
  assert(parentedX != parentedGizmo.axes.end());
  const runtime::Vec2 parentedHandle = midpoint(*parentedX);
  assert(!parentedTool
              .update(parentedWorld, "child", sceneView,
                      {.mousePosition = parentedHandle,
                       .viewportSize = viewport,
                       .hovered = true,
                       .focused = true,
                       .leftPressed = true,
                       .leftDown = true})
              .edit);
  const runtime::Vec2 parentedDelta{parentedX->end.x - parentedX->start.x,
                                    parentedX->end.y - parentedX->start.y};
  const auto parentedEdit =
      parentedTool.update(parentedWorld, "child", sceneView,
                          {.mousePosition = parentedHandle,
                           .mouseDelta = parentedDelta,
                           .viewportSize = viewport,
                           .hovered = true,
                           .focused = true,
                           .leftDown = true,
                           .bypassSnapping = true});
  assert(parentedEdit.edit);
  assert(parentedEdit.edit->value.at(0).get<float>() > 1.0F);
  assert(close(parentedEdit.edit->value.at(1).get<float>(), 0.0F));

  editor::EditorWorkspace workspace;
  std::string error;
  assert(workspace.open(std::filesystem::path(DEMI_SOURCE_DIR) /
                            "examples/production_2d_foundation",
                        error));
  assert(workspace.sceneDomain() == editor::EditorSceneDomain::TwoDimensional);
  assert(workspace.viewDimension() ==
         editor::EditorSceneViewDimension::TwoDimensional);
  const auto editable = std::ranges::find_if(
      workspace.project().world.entities, [](const runtime::Entity &entity) {
        return entity.enabled &&
               entity.hasComponent<runtime::Transform2DComponent>() &&
               entity.hasComponent<runtime::SpriteComponent>();
      });
  assert(editable != workspace.project().world.entities.end());
  workspace.selectEntity(editable->id);
  assert(workspace.sceneView2D().frameEntity(workspace.project().world,
                                             editable->id));
  workspace.sceneView2D().translationSnap = 0.0F;
  const std::string before = workspace.sceneDocument().json().dump();
  const auto workspaceGizmo = workspace.gizmoPresentation2D(viewport);
  assert(!workspaceGizmo.axes.empty());
  const runtime::Vec2 workspaceHandle = midpoint(workspaceGizmo.axes.front());
  assert(workspace.updateViewportTool2D({.mousePosition = workspaceHandle,
                                         .viewportSize = viewport,
                                         .hovered = true,
                                         .focused = true,
                                         .leftPressed = true,
                                         .leftDown = true},
                                        error));
  const runtime::Vec2 direction{
      workspaceGizmo.axes.front().end.x - workspaceGizmo.axes.front().start.x,
      workspaceGizmo.axes.front().end.y - workspaceGizmo.axes.front().start.y};
  assert(workspace.updateViewportTool2D({.mousePosition = workspaceHandle,
                                         .mouseDelta = direction,
                                         .viewportSize = viewport,
                                         .hovered = true,
                                         .focused = true,
                                         .leftDown = true},
                                        error));
  assert(workspace.sceneDocument().json().dump() != before);
  assert(workspace.updateViewportTool2D(
      {.viewportSize = viewport, .focused = true, .leftReleased = true},
      error));
  assert(workspace.undo(error));
  assert(workspace.sceneDocument().json().dump() == before);

  editor::EditorWorkspace isometricWorkspace;
  assert(isometricWorkspace.open(std::filesystem::path(DEMI_SOURCE_DIR) /
                                     "examples/isometric_base_builder",
                                 error));
  const editor::EditorIsoGridCell paintedRoad{
      .gridEntityId = "ent_iso_grid", .x = 4, .y = 9};
  const auto paintedCells = editor::paintedIsoGridCells(
      isometricWorkspace.project().world, "ent_iso_grid");
  assert(std::ranges::find(paintedCells, paintedRoad) != paintedCells.end());
  const nlohmann::json *gridDocument =
      isometricWorkspace.sceneDocument().component("ent_iso_grid", "IsoGrid");
  assert(gridDocument != nullptr);
  auto movedCells = editor::moveAuthoredIsoGridCell(*gridDocument, paintedRoad,
                                                    5, 9, 18, 18, error);
  assert(movedCells && movedCells->contains("5,9") &&
         !movedCells->contains("4,9"));
  assert(!editor::moveAuthoredIsoGridCell(*gridDocument, paintedRoad, 3, 9, 18,
                                          18, error));
  const auto paintedRoadPosition = editor::isoGridCellWorldPosition(
      isometricWorkspace.project().world, paintedRoad);
  assert(paintedRoadPosition);
  const runtime::Vec2 paintedRoadScreen =
      editor::projectScenePoint2D(isometricWorkspace.sceneView2D().camera(),
                                  *paintedRoadPosition, viewport);
  assert(editor::pickPaintedIsoGridCell(
             isometricWorkspace.project().world,
             isometricWorkspace.sceneView2D().camera(), paintedRoadScreen,
             viewport) == paintedRoad);
  isometricWorkspace.selectIsoGridCell(paintedRoad);
  assert(isometricWorkspace.sceneView2D().frameGridCell(
      isometricWorkspace.project().world, paintedRoad));
  const auto authoredRoadGizmo =
      isometricWorkspace.gizmoPresentation2D(viewport);
  assert(authoredRoadGizmo.axes.size() == 2);
  const std::string beforeRoadMove =
      isometricWorkspace.sceneDocument().json().dump();
  const runtime::Vec2 roadHandle = midpoint(authoredRoadGizmo.axes.front());
  assert(isometricWorkspace.updateViewportTool2D({.mousePosition = roadHandle,
                                                  .viewportSize = viewport,
                                                  .hovered = true,
                                                  .focused = true,
                                                  .leftPressed = true,
                                                  .leftDown = true},
                                                 error));
  const runtime::Vec2 roadDelta{authoredRoadGizmo.axes.front().end.x -
                                    authoredRoadGizmo.axes.front().start.x,
                                authoredRoadGizmo.axes.front().end.y -
                                    authoredRoadGizmo.axes.front().start.y};
  assert(isometricWorkspace.updateViewportTool2D({.mousePosition = roadHandle,
                                                  .mouseDelta = roadDelta,
                                                  .viewportSize = viewport,
                                                  .hovered = true,
                                                  .focused = true,
                                                  .leftDown = true},
                                                 error));
  assert(isometricWorkspace.updateViewportTool2D(
      {.viewportSize = viewport, .focused = true, .leftReleased = true},
      error));
  assert(isometricWorkspace.sceneDocument().json().dump() != beforeRoadMove);
  assert(isometricWorkspace.undo(error));
  assert(isometricWorkspace.sceneDocument().json().dump() == beforeRoadMove);
  return 0;
}
