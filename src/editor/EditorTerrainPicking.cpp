#include "editor/EditorTerrainPicking.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "editor/EditorViewportProjection.h"
#include <algorithm>
#include <cmath>

namespace demi::editor {
std::optional<runtime::Vec3>
pickEditorTerrain(const runtime::World &world, std::string_view entityId,
                  EditorTerrainSurfacePtr surface,
                  const EditorSceneViewCamera &camera,
                  const EditorViewportToolInput &input) {
  if (!surface || input.viewportSize.x <= 0 || input.viewportSize.y <= 0)
    return std::nullopt;
  const auto *entity = runtime::findEntity(world, std::string(entityId));
  const auto transform =
      entity ? runtime::resolveWorldTransform3D(world, *entity) : std::nullopt;
  if (!transform || std::abs(transform->scale.x) < 1e-6F ||
      std::abs(transform->scale.y) < 1e-6F ||
      std::abs(transform->scale.z) < 1e-6F)
    return std::nullopt;
  const auto ray =
      sceneViewportRay(camera, input.mousePosition, input.viewportSize);
  return surface->raycast(
      runtime::inverseTransformPoint3D(*transform, ray.origin),
      runtime::inverseTransformVector3D(*transform, ray.direction));
}

std::vector<std::optional<runtime::Vec2>> projectEditorTerrainBrush(
    const runtime::World &world, const EditorTerrainAuthoring &authoring,
    const EditorSceneViewCamera &camera, runtime::Vec2 viewportSize) {
  std::vector<std::optional<runtime::Vec2>> result;
  const auto *entity = runtime::findEntity(world, authoring.entityId());
  const auto transform =
      entity ? runtime::resolveWorldTransform3D(world, *entity) : std::nullopt;
  if (!transform)
    return result;
  for (const auto point : authoring.brushRing())
    result.push_back(
        std::isfinite(point.y)
            ? projectScenePoint3D(camera,
                                  runtime::transformPoint3D(*transform, point),
                                  viewportSize)
            : std::nullopt);
  return result;
}

std::vector<EditorTerrainProjectedMaskSample> projectEditorTerrainExclusions(
    const runtime::World &world, const EditorTerrainAuthoring &authoring,
    const EditorSceneViewCamera &camera, runtime::Vec2 viewportSize) {
  std::vector<EditorTerrainProjectedMaskSample> result;
  if (!authoring.previewExclusions || viewportSize.x <= 0 ||
      viewportSize.y <= 0)
    return result;
  const auto *entity = runtime::findEntity(world, authoring.entityId());
  const auto transform =
      entity ? runtime::resolveWorldTransform3D(world, *entity) : std::nullopt;
  if (!transform)
    return result;
  const auto budget = static_cast<std::size_t>(
      std::max(1.0F, viewportSize.x * viewportSize.y / 64));
  for (const auto &sample : authoring.exclusionPreview(budget)) {
    const auto projected = projectScenePoint3D(
        camera, runtime::transformPoint3D(*transform, sample.position),
        viewportSize);
    if (projected && projected->x >= 0 && projected->x <= viewportSize.x &&
        projected->y >= 0 && projected->y <= viewportSize.y)
      result.push_back({*projected, sample.weight});
  }
  return result;
}
} // namespace demi::editor
