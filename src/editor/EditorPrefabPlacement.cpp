#include "editor/EditorPrefabPlacement.h"

#include "editor/EditorTerrainRuntime.h"
#include "editor/EditorViewportProjection.h"
#include "editor/EditorViewportProjection2D.h"

#include "demi/runtime/isometric/IsoGridMath.h"
#include "demi/runtime/isometric/IsoWorldQueries.h"
#include "demi/runtime/scene/SceneEntityParser.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/2dcomponents/IsoTransformComponent.h"
#include "demi/runtime/scene/components/2dcomponents/Transform2DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"

#include <cmath>
#include <exception>
#include <fstream>
#include <limits>
#include <utility>
#include <vector>

namespace demi::editor {
namespace {

std::optional<nlohmann::json>
expandedPrefabSource(const std::filesystem::path &path, std::string &error) {
  std::ifstream input(path);
  if (!input) {
    error = "Could not open prefab source: " + path.string();
    return std::nullopt;
  }
  try {
    nlohmann::json source;
    input >> source;
    auto expansion = runtime::composition::expandScene(path, source, false);
    if (expansion.document)
      return std::move(*expansion.document);
    error = expansion.diagnostics.empty()
                ? "Could not resolve the prefab for placement."
                : expansion.diagnostics.front().code + ": " +
                      expansion.diagnostics.front().message;
  } catch (const nlohmann::json::exception &exception) {
    error = "Could not parse prefab source: " + std::string(exception.what());
  }
  return std::nullopt;
}

struct Root2D {
  std::string id;
  runtime::Vec2 position;
};

struct Root3D {
  std::string id;
  runtime::Vec3 position;
};

struct IsoRoot {
  std::string id;
  runtime::Vec2 tile;
};

struct PrefabRoots {
  std::vector<Root2D> twoDimensional;
  std::vector<Root3D> threeDimensional;
  std::vector<IsoRoot> isometric;
};

std::optional<PrefabRoots>
prefabRoots(const std::filesystem::path &path, std::string &error) {
  const auto expanded = expandedPrefabSource(path, error);
  if (!expanded)
    return std::nullopt;
  const auto entities = expanded->find("entities");
  if (entities == expanded->end() || !entities->is_array()) {
    error = "The prefab has no entities array.";
    return std::nullopt;
  }

  PrefabRoots roots;
  try {
    for (const nlohmann::json &item : *entities) {
      if (!item.is_object())
        continue;
      const runtime::Entity entity =
          runtime::scene_loading::parseSceneEntity(item);
      if (const auto *transform =
              entity.component<runtime::Transform2DComponent>();
          transform != nullptr && transform->parent.empty())
        roots.twoDimensional.push_back({entity.id, transform->position});
      if (const auto *transform =
              entity.component<runtime::Transform3DComponent>();
          transform != nullptr && transform->parent.empty())
        roots.threeDimensional.push_back({entity.id, transform->position});
      if (const auto *transform =
              entity.component<runtime::IsoTransformComponent>();
          transform != nullptr && transform->parent.empty())
        roots.isometric.push_back({entity.id, transform->tile});
    }
  } catch (const std::exception &exception) {
    error = "Could not read prefab transforms: " +
            std::string(exception.what());
    return std::nullopt;
  }
  return roots;
}

} // namespace

runtime::Vec2
prefabDropWorldPosition2D(const EditorSceneView2DCamera &camera,
                          const runtime::Vec2 viewportPosition,
                          const runtime::Vec2 viewportSize) {
  return unprojectScenePoint2D(camera, viewportPosition, viewportSize);
}

std::optional<runtime::Vec3>
prefabDropWorldPosition3D(const EditorSceneViewCamera &camera,
                          const runtime::Vec2 viewportPosition,
                          const runtime::Vec2 viewportSize) {
  return intersectSceneGroundPlane3D(camera, viewportPosition, viewportSize);
}

runtime::Vec3 sceneDropWorldPosition3D(const EditorSceneViewCamera &camera,
                                      const runtime::World &world,
                                      const runtime::Vec2 viewportPosition,
                                      const runtime::Vec2 viewportSize) {
  const EditorViewportRay ray =
      sceneViewportRay(camera, viewportPosition, viewportSize);
  std::optional<runtime::Vec3> closest;
  float closestDistanceSquared = std::numeric_limits<float>::infinity();
  for (const runtime::Entity &entity : world.entities) {
    if (!entity.enabled || !entity.component<runtime::Terrain3DComponent>())
      continue;
    const auto surface = currentEditorTerrain(world, entity.id);
    const auto transform = runtime::resolveWorldTransform3D(world, entity);
    if (!surface || !transform || std::abs(transform->scale.x) < 1e-6F ||
        std::abs(transform->scale.y) < 1e-6F ||
        std::abs(transform->scale.z) < 1e-6F)
      continue;
    const auto local = surface->raycast(
        runtime::inverseTransformPoint3D(*transform, ray.origin),
        runtime::inverseTransformVector3D(*transform, ray.direction));
    if (!local)
      continue;
    const runtime::Vec3 hit = runtime::transformPoint3D(*transform, *local);
    const float dx = hit.x - ray.origin.x;
    const float dy = hit.y - ray.origin.y;
    const float dz = hit.z - ray.origin.z;
    const float distanceSquared = dx * dx + dy * dy + dz * dz;
    if (distanceSquared < closestDistanceSquared) {
      closest = hit;
      closestDistanceSquared = distanceSquared;
    }
  }
  if (closest)
    return *closest;
  // A nearly horizontal ray can meet Y=0 far outside the visible work area.
  if (const auto ground =
          intersectSceneGroundPlane3D(camera, viewportPosition, viewportSize)) {
    const float dx = ground->x - ray.origin.x;
    const float dy = ground->y - ray.origin.y;
    const float dz = ground->z - ray.origin.z;
    const float workingDistance = std::max(camera.focusDistance, 0.01F);
    if (dx * dx + dy * dy + dz * dz <=
        workingDistance * workingDistance * 16.0F)
      return *ground;
  }
  const float workingDistance = std::max(camera.focusDistance, 0.01F);
  return {ray.origin.x + ray.direction.x * workingDistance,
          ray.origin.y + ray.direction.y * workingDistance,
          ray.origin.z + ray.direction.z * workingDistance};
}

std::optional<nlohmann::json>
prefabPlacementOverrides(const std::filesystem::path &path,
                         const runtime::Vec3 groundPosition,
                         std::string &error) {
  const auto roots = prefabRoots(path, error);
  if (!roots)
    return std::nullopt;
  if (roots->threeDimensional.empty()) {
    error = "The prefab has no root Transform3D for 3D placement.";
    return std::nullopt;
  }
  const runtime::Vec3 anchor = roots->threeDimensional.front().position;
  const runtime::Vec3 offset{groundPosition.x - anchor.x, groundPosition.y,
                             groundPosition.z - anchor.z};
  nlohmann::json overrides = nlohmann::json::object();
  for (const Root3D &root : roots->threeDimensional) {
    const runtime::Vec3 position{root.position.x + offset.x,
                                 root.position.y + offset.y,
                                 root.position.z + offset.z};
    overrides[root.id]["components"]["Transform3D"]["position"] =
        {position.x, position.y, position.z};
  }
  return overrides;
}

std::optional<nlohmann::json>
prefabPlacementOverrides(const std::filesystem::path &path,
                         const runtime::World &world,
                         const runtime::Vec2 worldPosition,
                         std::string &error) {
  const auto roots = prefabRoots(path, error);
  if (!roots)
    return std::nullopt;
  nlohmann::json overrides = nlohmann::json::object();
  if (!roots->twoDimensional.empty()) {
    const runtime::Vec2 anchor = roots->twoDimensional.front().position;
    const runtime::Vec2 offset{worldPosition.x - anchor.x,
                               worldPosition.y - anchor.y};
    for (const Root2D &root : roots->twoDimensional) {
      const runtime::Vec2 position{root.position.x + offset.x,
                                   root.position.y + offset.y};
      overrides[root.id]["components"]["Transform2D"]["position"] =
          {position.x, position.y};
    }
  }
  if (!roots->isometric.empty()) {
    const auto grid = runtime::isometric::gridDefinition(world);
    if (!grid) {
      error = "An isometric prefab requires an IsoGrid in the active scene.";
      return std::nullopt;
    }
    const runtime::isometric::GridCell target =
        runtime::isometric::worldToTile(*grid, worldPosition);
    const runtime::Vec2 anchor = roots->isometric.front().tile;
    const runtime::Vec2 offset{static_cast<float>(target.x) - anchor.x,
                               static_cast<float>(target.y) - anchor.y};
    for (const IsoRoot &root : roots->isometric) {
      const runtime::Vec2 tile{root.tile.x + offset.x,
                               root.tile.y + offset.y};
      overrides[root.id]["components"]["IsoTransform"]["tile"] =
          {tile.x, tile.y};
    }
  }
  if (overrides.empty()) {
    error = "The prefab has no root Transform2D or IsoTransform for 2D "
            "placement.";
    return std::nullopt;
  }
  return overrides;
}

} // namespace demi::editor
