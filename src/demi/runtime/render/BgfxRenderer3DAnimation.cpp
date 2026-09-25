#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"

#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/geometry/MeshDeformation3D.h"
#include "demi/runtime/render/bgfx3d/MeshTransform3D.h"
#include "demi/runtime/render/bgfx3d/MeshVertexPreparation3D.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"

#include <bit>

namespace demi::runtime::render {
namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start)
      .count();
}

std::uint64_t poseRevision(const AnimationPlayer3DComponent &player, int clip,
                           const WorldTransform3D &transform, Vec3 size) {
  std::uint64_t hash = 14695981039346656037ULL;
  const auto mix = [&](std::uint64_t value) {
    hash ^= value;
    hash *= 1099511628211ULL;
  };
  mix(static_cast<std::uint32_t>(clip));
  mix(player.loop);
  mix(player.proceduralPoseRevision);
  // Procedural targets are world-space, so changing the model transform changes
  // the sampled local pose even when clip time and target revision stay fixed.
  if (!player.boneSegments.empty())
    for (float value : composeMeshTransform3D(transform, size))
      mix(std::bit_cast<std::uint32_t>(value));
  return hash;
}
} // namespace

bool BgfxRenderer3D::prepareAnimatedMeshes(
    std::span<const VisibleMesh3D> visible, const BgfxCameraFrame3D &frame,
    std::string &error) {
  ProfileScope wall("Renderer3D.animation_prepare_wall");
  struct Work {
    const VisibleMesh3D *visible;
    const MeshRendererComponent *mesh;
    const AnimationPlayer3DComponent *player;
    const assets::GltfSkinnedModel3D *source;
    const MeshGeometry3D *rest;
    CachedMesh *target;
    int clip;
    std::uint64_t signature;
    std::vector<GpuMeshVertex3D> vertices;
    std::string error;
    bool success = false;
    double skinMs = 0, verticesMs = 0;
    bool useGpu = false;
    std::vector<float> palette;
    const GpuSkinPaletteLayout *gpuLayout = nullptr;
    float visualRate = 0;
    const std::string *selectedModel = nullptr;
  };
  // Bound temporary vertex storage independently of crowd population. An
  // individually oversized model is processed alone; nothing is decimated.
  constexpr std::size_t VertexBudget = 256 * 1024;
  constexpr std::size_t MaximumBatch = 16;
  constexpr std::size_t MaximumPaletteBatch = 128;
  std::vector<Work> batch;
  batch.reserve(MaximumPaletteBatch);
  std::size_t verticesInBatch = 0;
  bool batchHasCpu = false;
  std::size_t peakVertices = 0;
  std::size_t peakBatch = 0;
  std::size_t gpuCount = 0, cpuCount = 0;
  std::size_t deferredCount = 0;
  const auto flush = [&]() -> bool {
    if (batch.empty())
      return true;
    try {
      extractionJobs_.parallelFor(batch.size(), 1, [&](std::size_t index) {
        Work &work = batch[index];
        if (work.useGpu) {
          const auto start = Clock::now();
          assets::GltfSkinnedModel3D::Pose pose;
          work.success = work.clip >= 0
              ? work.source->samplePose(work.clip, work.player->time, work.player->loop, pose, work.error)
              : work.source->bindPose(pose, work.error);
          if (work.success)
            work.success = packGpuSkinPalette(*work.gpuLayout, pose, work.palette, work.error);
          work.skinMs = elapsed(start);
          return;
        }
        std::vector<Vec3> positions;
        assets::GltfSkinnedModel3D::BoneSegments segments;
        WorldTransform3D modelTransform = work.visible->transform;
        const auto size = work.mesh->size;
        modelTransform.scale = {modelTransform.scale.x * size.x,
                                modelTransform.scale.y * size.y,
                                modelTransform.scale.z * size.z};
        for (const auto &[bone, target] : work.player->boneSegments) {
          segments.emplace(
              bone,
              assets::GltfSkinnedModel3D::BoneSegment{
                  .start =
                      inverseTransformPoint3D(modelTransform, target.start),
                  .end = inverseTransformPoint3D(modelTransform, target.end),
                  .pole =
                      inverseTransformPoint3D(modelTransform, target.pole)});
        }
        auto start = Clock::now();
        const bool sampled =
            work.clip >= 0
                ? work.source->samplePositions(work.clip, work.player->time,
                                               work.player->loop, positions,
                                               work.error, segments)
                : work.source->bindPosePositions(positions, work.error,
                                                 segments);
        work.skinMs = elapsed(start);
        if (!sampled)
          return;
        start = Clock::now();
        work.success = prepareMeshVertices3D(
            positions, work.rest->uvs, work.rest->indices, 0xffffffffU, {},
            work.rest->colors, work.vertices, work.error);
        work.verticesMs = elapsed(start);
      });
    } catch (const std::exception &failure) {
      error =
          std::string("Animated mesh preparation failed: ") + failure.what();
      return false;
    }
    // Workers only read immutable scene/model inputs and write their own
    // output. All GPU resource changes and profiler records stay on the render
    // thread.
    for (const Work &work : batch) {
      if (!work.success) {
        error = work.visible->entity->id + ": " + work.error;
        return false;
      }
    }
    for (Work &work : batch) {
      const VisualAnimationSample3D stamp{work.player->visualClock, work.player->time, work.visualRate};
      if (work.useGpu) {
        work.target->gpu.clear();
        work.target->skinPalette = std::move(work.palette);
        work.target->signature = work.signature;
        work.target->animationModel = *work.selectedModel;
        work.target->animationSample = stamp;
        RuntimeProfiler::record("Renderer3D.skin_palette_cpu", work.skinMs);
        RuntimeProfiler::record("Renderer3D.animation_rebuild", work.skinMs);
        continue;
      }
      const auto start = Clock::now();
      if (work.target->animationModel != *work.selectedModel)
        work.target->gpu.clear();
      if (!work.target->gpu.uploadPrepared(work.vertices, work.rest->indices,
                                           error, true)) {
        error = work.visible->entity->id + ": " + error;
        return false;
      }
      const double uploadMs = elapsed(start);
      RuntimeProfiler::record("Renderer3D.skin_cpu", work.skinMs);
      RuntimeProfiler::record("Renderer3D.mesh_vertices_cpu", work.verticesMs);
      RuntimeProfiler::record("Renderer3D.mesh_buffer_update_cpu", uploadMs);
      RuntimeProfiler::record("Renderer3D.skin_upload_cpu",
                              work.verticesMs + uploadMs);
      RuntimeProfiler::record("Renderer3D.animation_rebuild",
                              work.skinMs + work.verticesMs + uploadMs);
      work.target->signature = work.signature;
      work.target->animationModel = *work.selectedModel;
      work.target->skinPalette.clear();
      work.target->animationSample = stamp;
    }
    batch.clear();
    verticesInBatch = 0;
    batchHasCpu = false;
    return true;
  };

  for (const auto &item : visible) {
    const auto *mesh = item.entity->component<MeshRendererComponent>();
    const auto *player = item.entity->component<AnimationPlayer3DComponent>();
    if (player && item.entity->hasComponent<MeshInstances3DComponent>()) {
      error = "MeshInstances3D currently requires a shared static mesh, "
              "without AnimationPlayer3D";
      return false;
    }
    if (!player || !mesh || mesh->model.empty() || !mesh->vertices.empty())
      continue;
    const auto lodPosition=frame.lodPosition.value_or(frame.position);
    const Vec3 delta{item.transform.position.x - lodPosition.x,
                     item.transform.position.y - lodPosition.y,
                     item.transform.position.z - lodPosition.z};
    if (mesh->cullDistance > 0 &&
        delta.x * delta.x + delta.y * delta.y + delta.z * delta.z >=
            mesh->cullDistance * mesh->cullDistance)
      continue;
    const auto lod=selectModelLod(*mesh, player, item.transform.position, lodPosition,
                                  !entityMeshDents3D(*item.entity).empty());
    if (lod.isCulled) continue;
    const auto &selectedModel=*lod.model;
    const auto source = animatedModels_.find(selectedModel);
    const auto rest = modelMeshes_.find(selectedModel);
    if (source == animatedModels_.end() || rest == modelMeshes_.end()) {
      error = "No skinned model data is loaded for " + selectedModel + ".";
      return false;
    }
    auto &cached = dynamicMeshes_[item.entity->id];
    if (!cached)
      cached = std::make_unique<CachedMesh>(resources_);
    const int clip = source->second.clipIndex(player->clipName, 0);
    const auto signature =
        poseRevision(*player, clip, item.transform, mesh->size);
    const bool useGpu = rest->second->gpuSkin && mesh->material.empty() &&
        player->boneSegments.empty() && player->layers.empty() && player->blendWeight == 1;
    if (useGpu) ++gpuCount; else ++cpuCount;
    const bool ready = useGpu ? !cached->skinPalette.empty()
                             : cached->skinPalette.empty() && cached->gpu.valid();
    const float visualRate = player->playing && player->boneSegments.empty() &&
        player->layers.empty() && player->blendWeight == 1 &&
        delta.x*delta.x + delta.y*delta.y + delta.z*delta.z >=
            player->visualUpdateDistance * player->visualUpdateDistance
        ? player->visualUpdateRate : 0;
    if (ready && cached->animationModel == selectedModel && cached->signature == signature) {
      if (cached->animationSample.time == player->time) continue;
      if (!visualAnimationSampleDue(cached->animationSample, player->visualClock,
                                    player->time, visualRate, item.entity->id)) {
        ++deferredCount;
        continue;
      }
    }
    const auto count = useGpu ? 0 : source->second.vertices.size();
    // Palette-only jobs have tiny output. Mixed/CPU batches retain the old
    // limit so heavy vertex work stays evenly distributed across workers.
    const auto batchLimit = useGpu && !batchHasCpu ? MaximumPaletteBatch : MaximumBatch;
    if (!batch.empty() &&
        (batch.size() >= batchLimit || verticesInBatch > VertexBudget ||
         count > VertexBudget - std::min(verticesInBatch, VertexBudget)))
      if (!flush())
        return false;
    batch.push_back({.visible = &item,
                     .mesh = mesh,
                     .player = player,
                     .source = &source->second,
                     .rest = &rest->second->restGeometry,
                     .target = cached.get(),
                     .clip = clip,
                     .signature = signature,
                     .vertices = {},
                     .error = {}, .useGpu = useGpu, .palette = {},
                     .gpuLayout = useGpu ? &rest->second->gpuSkin->paletteLayout() : nullptr,
                     .visualRate = visualRate, .selectedModel=&selectedModel});
    verticesInBatch += count;
    batchHasCpu = batchHasCpu || !useGpu;
    peakVertices = std::max(peakVertices, verticesInBatch);
    peakBatch = std::max(peakBatch, batch.size());
  }
  const bool success = flush();
  RuntimeProfiler::setGauge("Renderer3D.animation_batch_vertices",
                            peakVertices);
  RuntimeProfiler::setGauge("Renderer3D.animation_batch_meshes", peakBatch);
  RuntimeProfiler::setGauge("Renderer3D.animation_workers_available",
                            extractionJobs_.workerCount());
  RuntimeProfiler::setGauge("Renderer3D.gpu_skinned_meshes", gpuCount);
  RuntimeProfiler::setGauge("Renderer3D.cpu_skinned_meshes", cpuCount);
  RuntimeProfiler::setGauge("Renderer3D.animation_deferred", deferredCount);
  return success;
}

} // namespace demi::runtime::render
