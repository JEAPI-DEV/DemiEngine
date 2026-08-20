#include "editor/EditorWorkspace.h"
#include "editor/EditorViewportProjection.h"

#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/3dcomponents/Camera3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"

#include <cassert>
#include <cmath>
#include <filesystem>
#include <ranges>
#include <string>

namespace {

bool close(const float left, const float right) {
  return std::abs(left - right) < 0.001F;
}

bool close(const demi::runtime::Vec3 left, const demi::runtime::Vec3 right) {
  return close(left.x, right.x) && close(left.y, right.y) &&
         close(left.z, right.z);
}

} // namespace

int main() {
  const std::filesystem::path root = DEMI_SOURCE_DIR;
  demi::editor::EditorWorkspace workspace;
  std::string error;
  assert(workspace.open(root / "examples/minimal_3d", error));

  const auto authoredCamera = std::ranges::find_if(
      workspace.project().world.entities, [](const auto &entity) {
        return entity.enabled &&
               entity.template hasComponent<demi::runtime::Camera3DComponent>();
      });
  assert(authoredCamera != workspace.project().world.entities.end());
  const auto authoredTransform = demi::runtime::resolveWorldTransform3D(
      workspace.project().world, *authoredCamera);
  assert(authoredTransform.has_value());
  assert(close(workspace.sceneView().camera().position,
               authoredTransform->position));

  const std::string authoredJson = workspace.sceneDocument().json().dump();
  const auto start = workspace.sceneView().camera();
  workspace.sceneView().update({.deltaSeconds = 0.1F,
                                .hovered = true,
                                .focused = true,
                                .flyButton = true,
                                .moveForward = true});
  assert(workspace.sceneView().capturesPointer());
  assert(!close(workspace.sceneView().camera().position, start.position));
  assert(workspace.sceneDocument().json().dump() == authoredJson);

  workspace.sceneView().update({});
  assert(!workspace.sceneView().capturesPointer());
  const auto inactive = workspace.sceneView().camera();
  workspace.sceneView().update(
      {.deltaSeconds = 0.1F, .flyButton = true, .moveForward = true});
  assert(close(workspace.sceneView().camera().position, inactive.position));

  const auto beforeRefresh = workspace.sceneView().camera();
  assert(workspace.refresh(error));
  assert(
      close(workspace.sceneView().camera().position, beforeRefresh.position));

  const auto namedEntity = std::ranges::find_if(
      workspace.project().world.entities, [&workspace](const auto &entity) {
        const auto *authored = workspace.sceneDocument().entity(entity.id);
        return authored != nullptr && authored->contains("name");
      });
  assert(namedEntity != workspace.project().world.entities.end());
  const auto beforeEdit = workspace.sceneView().camera();
  assert(workspace.editValue({.entityId = namedEntity->id, .field = "name"},
                             "Camera-state edit probe", false, error));
  assert(close(workspace.sceneView().camera().position, beforeEdit.position));
  assert(workspace.undo(error));

  const auto beforeRebuild = workspace.sceneView().camera();
  assert(workspace.createEntity(error));
  assert(
      close(workspace.sceneView().camera().position, beforeRebuild.position));
  assert(workspace.undo(error));

  const auto transformEntity = std::ranges::find_if(
      workspace.project().world.entities, [](const auto &entity) {
        return entity.enabled && entity.template hasComponent<
                                     demi::runtime::Transform3DComponent>();
      });
  assert(transformEntity != workspace.project().world.entities.end());
  assert(workspace.sceneView().frameEntity(workspace.project().world,
                                           transformEntity->id));

  workspace.sceneView().reset(workspace.project().world);
  const auto resetOnce = workspace.sceneView().camera();
  workspace.sceneView().update({.deltaSeconds = 0.1F,
                                .hovered = true,
                                .focused = true,
                                .flyButton = true,
                                .moveRight = true});
  const auto movedRight = workspace.sceneView().camera();
  const demi::runtime::Vec3 movement{
      movedRight.position.x - resetOnce.position.x,
      movedRight.position.y - resetOnce.position.y,
      movedRight.position.z - resetOnce.position.z};
  assert(demi::editor::projectSceneDirection3D(resetOnce, movement).x > 0.0F);
  workspace.sceneView().reset(workspace.project().world);
  assert(close(workspace.sceneView().camera().position, resetOnce.position));

  demi::editor::EditorSceneViewState oneStep;
  demi::editor::EditorSceneViewState twoSteps;
  oneStep.reset(workspace.project().world);
  twoSteps.reset(workspace.project().world);
  oneStep.update({.deltaSeconds = 0.1F,
                  .hovered = true,
                  .focused = true,
                  .flyButton = true,
                  .moveForward = true});
  twoSteps.update({.deltaSeconds = 0.05F,
                   .hovered = true,
                   .focused = true,
                   .flyButton = true,
                   .moveForward = true});
  twoSteps.update({.deltaSeconds = 0.05F,
                   .hovered = true,
                   .focused = true,
                   .flyButton = true,
                   .moveForward = true});
  assert(close(oneStep.camera().position, twoSteps.camera().position));

  workspace.sceneView().update({.deltaSeconds = 0.1F,
                                .hovered = true,
                                .focused = true,
                                .flyButton = true,
                                .moveRight = true});
  assert(workspace.open(root / "examples/minimal_voxel", error));
  demi::editor::EditorWorkspace freshWorkspace;
  assert(freshWorkspace.open(root / "examples/minimal_voxel", error));
  assert(close(workspace.sceneView().camera().position,
               freshWorkspace.sceneView().camera().position));
  return 0;
}
