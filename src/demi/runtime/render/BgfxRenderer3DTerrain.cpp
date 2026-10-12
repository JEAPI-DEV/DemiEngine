#include "demi/runtime/render/BgfxRenderer3D.h"

#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/bgfx3d/VisibleMeshGeometry3D.h"

#include <algorithm>

namespace demi::runtime::render {

void BgfxRenderer3D::beginPresentation() {
  if (presentationActive_) {
    return;
  }
  presentationActive_ = true;
  terrainStatistics_ = {};
  terrainUploadsAtPresentationStart_ = terrainMeshes_.uploads();
  terrainMeshes_.beginPresentation();
  terrainSelection_.beginPresentation();
}

void BgfxRenderer3D::endPresentation(bool succeeded) {
  if (!presentationActive_) {
    return;
  }
  terrainMeshes_.endPresentation(succeeded);
  terrainStatistics_.uploads =
      terrainMeshes_.uploads() - terrainUploadsAtPresentationStart_;
  terrainStatistics_.residentMeshes = terrainMeshes_.residentMeshes();
  terrainStatistics_.residentBytes = terrainMeshes_.residentBytes();
  RuntimeProfiler::setGauge("Renderer3D.terrain_gpu_meshes",
                            terrainStatistics_.residentMeshes);
  RuntimeProfiler::setGauge("Renderer3D.terrain_gpu_bytes",
                            terrainStatistics_.residentBytes);
  RuntimeProfiler::setGauge("Renderer3D.terrain_gpu_uploads",
                            terrainStatistics_.uploads);
  RuntimeProfiler::setGauge("Renderer3D.terrain_triangles",
                            terrainStatistics_.triangles);
  constexpr std::array names{
      "Renderer3D.terrain_lod_full", "Renderer3D.terrain_lod_half",
      "Renderer3D.terrain_lod_quarter", "Renderer3D.terrain_lod_coarse"};
  for (std::size_t level = 0; level < names.size(); ++level) {
    RuntimeProfiler::setGauge(names[level],
                              terrainStatistics_.selectedSurfaces[level]);
  }
  presentationActive_ = false;
}

bool BgfxRenderer3D::prepareTerrainMeshes(SceneVisibility3D &visibility,
                                          std::string &error) {
  const bool hasTerrain = std::ranges::any_of(
      visibility.meshes, [](const auto &draw) { return draw.terrainSurface; });
  if (!hasTerrain) {
    return true;
  }
  std::vector<VisibleMesh3D> prepared;
  prepared.reserve(visibility.meshes.size());
  for (std::size_t index = 0; index < visibility.meshes.size();) {
    auto draw = visibility.meshes[index];
    if (!draw.terrainSurface) {
      prepared.push_back(std::move(draw));
      ++index;
      continue;
    }
    // Extraction expands one surface into contiguous section draws. Upload and
    // any fallback happen once for that surface before ordering/compaction.
    std::size_t end = index + 1;
    while (end < visibility.meshes.size() &&
           visibility.meshes[end].entity == draw.entity &&
           visibility.meshes[end].terrainLevel == draw.terrainLevel) {
      ++end;
    }
    const auto upload = [&]() {
      return terrainMeshes_.get(
          draw.entity->id, draw.terrainLevel, visibleMeshGeometry3D(draw),
          draw.terrainGeometry, visibleMeshSections3D(draw), error);
    };
    draw.terrainGpuMesh = upload();
    if (!draw.terrainGpuMesh && draw.terrainLevel != 0) {
      // An upload failure must not leave a hole or replace gameplay geometry.
      // The Full variant is still resident in CPU memory and can be uploaded
      // transactionally while the previous GPU entry remains intact.
      draw.terrainGeometry.reset();
      draw.terrainSections = nullptr;
      draw.terrainLevel = 0;
      error.clear();
      draw.terrainGpuMesh = upload();
    }
    if (!draw.terrainGpuMesh) {
      return false;
    }
    dynamicMeshes_.erase(draw.entity->id);
    draw.materialSection.reset();
    const auto *sections = visibleMeshSections3D(draw);
    if (sections && !sections->sections.empty()) {
      for (std::size_t section = 0; section < sections->sections.size();
           ++section) {
        auto sectionDraw = draw;
        sectionDraw.materialSection = section;
        prepared.push_back(std::move(sectionDraw));
      }
    } else {
      prepared.push_back(std::move(draw));
    }
    index = end;
  }
  visibility.meshes = std::move(prepared);
  return true;
}

} // namespace demi::runtime::render
