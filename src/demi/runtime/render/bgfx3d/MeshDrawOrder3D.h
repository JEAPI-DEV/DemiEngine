#pragma once

#include "demi/runtime/destruction/DetachedFragmentFade3D.h"
#include "demi/runtime/render/bgfx3d/MeshSurface3D.h"
#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/scene/model/Entity.h"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <vector>

namespace demi::runtime::render {

// Opaque draws retain extraction order for batching. Transparent draws use
// camera depth, with source order breaking ties deterministically.
[[nodiscard]] inline std::size_t orderMeshSurfaces3D(
    std::vector<VisibleMesh3D> &meshes, const MaterialLibrary &materials,
    Vec3 cameraPosition, Vec3 cameraForward) {
  const auto isTransparent = [&](const VisibleMesh3D &visible) {
    const auto *mesh = visible.entity->component<MeshRendererComponent>();
    const auto *fade = visible.entity->component<FragmentOpacity3D>();
    return resolveMeshSurface3D(*mesh, materials.find(mesh->material),
                                fade ? std::optional<float>(fade->value)
                                     : std::nullopt)
        .transparent();
  };
  if (std::ranges::find_if(meshes, isTransparent) == meshes.end())
    return meshes.size();
  const auto depth = [&](const VisibleMesh3D &visible) {
    const Vec3 position = visible.transform.position;
    return (position.x - cameraPosition.x) * cameraForward.x +
           (position.y - cameraPosition.y) * cameraForward.y +
           (position.z - cameraPosition.z) * cameraForward.z;
  };
  const auto firstTransparent = std::stable_partition(
      meshes.begin(), meshes.end(),
      [&](const VisibleMesh3D &visible) { return !isTransparent(visible); });
  std::stable_sort(firstTransparent, meshes.end(),
                   [&](const auto &a, const auto &b) {
                     return depth(a) > depth(b);
                   });
  return static_cast<std::size_t>(firstTransparent - meshes.begin());
}

} // namespace demi::runtime::render
