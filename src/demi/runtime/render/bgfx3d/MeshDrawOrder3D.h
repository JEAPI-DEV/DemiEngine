#pragma once

#include "demi/runtime/destruction/DetachedFragmentFade3D.h"
#include "demi/runtime/render/bgfx3d/MeshSurface3D.h"
#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/render/bgfx3d/VisibleMeshGeometry3D.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/MeshMaterialSections3D.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

namespace demi::runtime::render {

// Opaque draws retain extraction order for batching. Transparent draws use
// camera depth, with source order breaking ties deterministically.
[[nodiscard]] inline std::size_t
orderMeshSurfaces3D(std::vector<VisibleMesh3D> &meshes,
                    const MaterialLibrary &materials, Vec3 cameraPosition,
                    Vec3 cameraForward) {
  const Entity *source = nullptr;
  const MeshRendererComponent *sourceMesh = nullptr;
  const FragmentOpacity3D *sourceFade = nullptr;
  const MeshMaterialSections3D *sourceSections = nullptr;
  const MeshMaterialSections3D *terrainSections = nullptr;
  std::vector<std::pair<std::optional<std::size_t>, bool>> sectionTransparency;
  const auto isTransparent = [&](const VisibleMesh3D &visible) {
    // Instance copies share immutable source state throughout this call.
    // Reuse its section decisions while copies stay adjacent, without a
    // persistent cache that could hide live material/fade edits next frame.
    if (source != visible.entity || terrainSections != visible.terrainSections) {
      source = visible.entity;
      terrainSections = visible.terrainSections;
      sourceMesh = source->component<MeshRendererComponent>();
      sourceFade = source->component<FragmentOpacity3D>();
      sourceSections = nullptr;
      sectionTransparency.clear();
    }
    const auto cached = std::ranges::find(
        sectionTransparency, visible.materialSection,
        &std::pair<std::optional<std::size_t>, bool>::first);
    if (cached != sectionTransparency.end()) {
      return cached->second;
    }
    const auto *material = materials.find(sourceMesh->material);
    if (visible.materialSection) {
      if (sourceSections == nullptr) {
        sourceSections = visibleMeshSections3D(visible);
      }
      const auto &section = sourceSections->sections.at(*visible.materialSection);
      material = materials.find(section.material);
    }
    const bool transparent =
        resolveMeshSurface3D(*sourceMesh, material,
                             sourceFade ? std::optional<float>(sourceFade->value)
                                        : std::nullopt)
            .transparent();
    sectionTransparency.emplace_back(visible.materialSection, transparent);
    return transparent;
  };
  if (std::ranges::find_if(meshes, isTransparent) == meshes.end()) {
    return meshes.size();
  }
  const auto depth = [&](const VisibleMesh3D &visible) {
    const Vec3 position = visible.transform.position;
    return (position.x - cameraPosition.x) * cameraForward.x +
           (position.y - cameraPosition.y) * cameraForward.y +
           (position.z - cameraPosition.z) * cameraForward.z;
  };
  const auto firstTransparent = std::stable_partition(
      meshes.begin(), meshes.end(),
      [&](const VisibleMesh3D &visible) { return !isTransparent(visible); });
  std::stable_sort(
      firstTransparent, meshes.end(),
      [&](const auto &a, const auto &b) { return depth(a) > depth(b); });
  return static_cast<std::size_t>(firstTransparent - meshes.begin());
}

} // namespace demi::runtime::render
