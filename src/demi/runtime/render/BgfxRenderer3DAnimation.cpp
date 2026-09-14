#include "demi/runtime/render/BgfxRenderer3D.h"

#include "demi/runtime/profiling/RuntimeProfiler.h"
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
  mix(std::bit_cast<std::uint32_t>(player.time));
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
  };
  // Bound temporary vertex storage independently of crowd population. An
  // individually oversized model is processed alone; nothing is decimated.
  constexpr std::size_t VertexBudget = 256 * 1024;
  constexpr std::size_t MaximumBatch = 16;
  std::vector<Work> batch;
  batch.reserve(MaximumBatch);
  std::size_t verticesInBatch = 0;
  std::size_t peakVertices = 0;
  std::size_t peakBatch = 0;
  const auto flush = [&]() -> bool {
    if (batch.empty())
      return true;
    try {
      extractionJobs_.parallelFor(batch.size(), 1, [&](std::size_t index) {
        Work &work = batch[index];
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
      const auto start = Clock::now();
      if (work.target->animationModel != work.mesh->model)
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
      work.target->animationModel = work.mesh->model;
    }
    batch.clear();
    verticesInBatch = 0;
    return true;
  };

  for (const auto &item : visible) {
    const auto *mesh = item.entity->component<MeshRendererComponent>();
    const auto *player = item.entity->component<AnimationPlayer3DComponent>();
    if (!player || !mesh || mesh->model.empty() || !mesh->vertices.empty())
      continue;
    const Vec3 delta{item.transform.position.x - frame.position.x,
                     item.transform.position.y - frame.position.y,
                     item.transform.position.z - frame.position.z};
    if (mesh->cullDistance > 0 &&
        delta.x * delta.x + delta.y * delta.y + delta.z * delta.z >=
            mesh->cullDistance * mesh->cullDistance)
      continue;
    const auto source = animatedModels_.find(mesh->model);
    const auto rest = modelMeshes_.find(mesh->model);
    if (source == animatedModels_.end() || rest == modelMeshes_.end()) {
      error = "No skinned model data is loaded for " + mesh->model + ".";
      return false;
    }
    auto &cached = dynamicMeshes_[item.entity->id];
    if (!cached)
      cached = std::make_unique<CachedMesh>(resources_);
    const int clip = source->second.clipIndex(player->clipName, 0);
    const auto signature =
        poseRevision(*player, clip, item.transform, mesh->size);
    if (cached->gpu.valid() && cached->animationModel == mesh->model &&
        cached->signature == signature)
      continue;
    const auto count = source->second.vertices.size();
    if (!batch.empty() &&
        (batch.size() == MaximumBatch ||
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
                     .error = {}});
    verticesInBatch += count;
    peakVertices = std::max(peakVertices, verticesInBatch);
    peakBatch = std::max(peakBatch, batch.size());
  }
  const bool success = flush();
  RuntimeProfiler::setGauge("Renderer3D.animation_batch_vertices",
                            peakVertices);
  RuntimeProfiler::setGauge("Renderer3D.animation_batch_meshes", peakBatch);
  RuntimeProfiler::setGauge("Renderer3D.animation_workers_available",
                            extractionJobs_.workerCount());
  return success;
}

} // namespace demi::runtime::render
