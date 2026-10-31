#include "demi/runtime/render/BgfxRenderer3D.h"

#include "demi/runtime/diagnostics/DeviceLog.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/bgfx3d/VisibleMeshGeometry3D.h"

#include <algorithm>

namespace demi::runtime::render {

void BgfxRenderer3D::beginPresentation() {
  if (presentationActive_) {
    return;
  }
  presentationActive_ = true;
  waterTimeAdvanced_ = false;
  cameraInstances_.reset();
  for (auto &instances : shadowInstances_) {
    instances.reset();
  }
  instancePreparation_.reset();
  primitiveSurfaces_.reset();
  opaqueCapture_.beginPresentation();
  waterRenderer_.beginPresentation();
  texturePresentationSerial_ = successfulTexturePresentation_ + 1;
  terrainStatistics_ = {};
  terrainUploadsAtPresentationStart_ = terrainMeshes_.uploads();
  terrainPatches_.beginPresentation();
  terrainGpuFields_.beginPresentation();
  terrainMaterials_.beginPresentation();
  terrainMeshes_.beginPresentation();
  terrainLodResidency_.beginPresentation();
  terrainCameraDemand_.beginPresentation();
  terrainSelection_.beginPresentation();
}

void BgfxRenderer3D::endPresentation(bool succeeded) {
  if (!presentationActive_) {
    return;
  }
  cameraInstances_.reset();
  for (auto &instances : shadowInstances_) {
    instances.reset();
  }
  opaqueCapture_.endPresentation();
  waterRenderer_.endPresentation();
  terrainGpuFields_.endPresentation();
  terrainMaterials_.endPresentation();
  terrainMeshes_.endPresentation(succeeded);
  terrainCameraDemand_.endPresentation();
  terrainLodResidency_.endPresentation();
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
  const auto cpu = terrainLodResidency_.statistics();
  if (cpu.failedJobs > reportedTerrainFailures_) {
    reportedTerrainFailures_ = cpu.failedJobs;
    terrainDiagnostic_ =
        "Terrain LOD preparation failed; Full terrain remains visible: " +
        terrainLodResidency_.lastError();
    deviceLogError(deviceLogMessage("terrain.lod", terrainDiagnostic_));
  }
  terrainStatistics_.cpuResidentBytes = cpu.residentBytes;
  terrainStatistics_.cpuPendingBytes = cpu.pendingBytes;
  terrainStatistics_.cpuPendingJobs = cpu.pendingJobs;
  terrainStatistics_.cpuReadyLevels = cpu.readyLevels;
  RuntimeProfiler::setGauge("Renderer3D.terrain_cpu_bytes", cpu.residentBytes);
  RuntimeProfiler::setGauge("Renderer3D.terrain_cpu_pending_bytes",
                            cpu.pendingBytes);
  RuntimeProfiler::setGauge("Renderer3D.terrain_cpu_pending_jobs",
                            cpu.pendingJobs);
  RuntimeProfiler::setGauge("Renderer3D.terrain_cpu_ready_levels",
                            cpu.readyLevels);
  RuntimeProfiler::setGauge("Renderer3D.terrain_cpu_submitted_jobs",
                            cpu.submittedJobs);
  RuntimeProfiler::setGauge("Renderer3D.terrain_cpu_discarded_jobs",
                            cpu.discardedJobs);
  RuntimeProfiler::setGauge("Renderer3D.terrain_cpu_failed_jobs",
                            cpu.failedJobs);
  RuntimeProfiler::setGauge("Renderer3D.terrain_cpu_budget_pressure",
                            cpu.budgetPressure);
  if (!terrainStatistics_.gpuFallbackReason.empty() &&
      terrainStatistics_.gpuFallbackReason != reportedTerrainGpuFallback_) {
    reportedTerrainGpuFallback_ = terrainStatistics_.gpuFallbackReason;
    deviceLog(deviceLogMessage("terrain.gpu", "CPU compatibility path: " +
                                                  reportedTerrainGpuFallback_));
  }
  RuntimeProfiler::setGauge("Renderer3D.terrain_patch_draws",
                            terrainStatistics_.gpuPatchDraws);
  RuntimeProfiler::setGauge("Renderer3D.terrain_patch_instances",
                            terrainStatistics_.gpuPatchInstances);
  RuntimeProfiler::setGauge("Renderer3D.terrain_patch_shadow_draws",
                            terrainStatistics_.gpuPatchShadowDraws);
  RuntimeProfiler::setGauge("Renderer3D.terrain_patch_shadow_instances",
                            terrainStatistics_.gpuPatchShadowInstances);
  RuntimeProfiler::setGauge("Renderer3D.terrain_patch_fallbacks",
                            terrainStatistics_.gpuPatchFallbacks);
  RuntimeProfiler::setGauge("Renderer3D.terrain_field_bytes",
                            terrainGpuFields_.residentBytes());
  RuntimeProfiler::setGauge("Renderer3D.terrain_field_pending_bytes",
                            terrainGpuFields_.pendingBytes());
  RuntimeProfiler::setGauge("Renderer3D.terrain_field_pending_jobs",
                            terrainGpuFields_.pendingJobs());
  RuntimeProfiler::setGauge("Renderer3D.terrain_field_upload_bytes",
                            terrainGpuFields_.uploadBytes());
  RuntimeProfiler::setGauge("Renderer3D.terrain_topology_bytes",
                            terrainPatches_.topologyBytes());
  RuntimeProfiler::setGauge("Renderer3D.terrain_material_bytes",
                            terrainMaterials_.residentBytes());
  RuntimeProfiler::setGauge("Renderer3D.terrain_material_pending_bytes",
                            terrainMaterials_.pendingBytes());
  RuntimeProfiler::setGauge("Renderer3D.terrain_material_pending_jobs",
                            terrainMaterials_.pendingJobs());
  RuntimeProfiler::setGauge("Renderer3D.terrain_material_upload_bytes",
                            terrainMaterials_.uploadBytes());
  finishMaterialTexturePresentation(succeeded);
  presentationActive_ = false;
}

void BgfxRenderer3D::tickInactiveTerrain() {
  if (presentationActive_) {
    return;
  }
  // A hidden/skipped view retires terrain demand, not resident model/material
  // assets. Those textures are owned by asset publication and normal material
  // presentation; discarding them here causes synchronous decode on resumption.
  terrainPatches_.clear();
  terrainGpuFields_.clear();
  terrainMaterials_.clear();
  terrainGpuFields_.pollCompleted();
  terrainLodResidency_.tickInactive();
  const auto cpu = terrainLodResidency_.statistics();
  terrainStatistics_.cpuResidentBytes = cpu.residentBytes;
  terrainStatistics_.cpuPendingBytes = cpu.pendingBytes;
  terrainStatistics_.cpuPendingJobs = cpu.pendingJobs;
  terrainStatistics_.cpuReadyLevels = cpu.readyLevels;
  terrainCameraDemand_.clear();
  terrainMeshes_.clear();
  terrainSelection_.clear();
  terrainStatistics_.residentMeshes = terrainMeshes_.residentMeshes();
  terrainStatistics_.residentBytes = terrainMeshes_.residentBytes();
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
