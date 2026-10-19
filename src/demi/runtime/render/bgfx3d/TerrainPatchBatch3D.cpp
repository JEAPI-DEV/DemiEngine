#include "demi/runtime/render/bgfx3d/TerrainPatchBatch3D.h"

#include "demi/runtime/destruction/DetachedFragmentFade3D.h"
#include "demi/runtime/geometry/MeshDeformation3D.h"
#include "demi/runtime/render/bgfx3d/MeshTransform3D.h"
#include "demi/runtime/render/bgfx3d/TerrainCameraDemand3D.h"
#include "demi/runtime/render/bgfx3d/TerrainMaterialResidency3D.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/SurfaceRelief3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/model/World.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime::render {
namespace {

struct GpuTopology {
  explicit GpuTopology(GpuResources &owner) : resources(owner) {}
  ~GpuTopology() {
    if (vertices) {
      resources.destroy(vertices);
    }
    if (indices) {
      resources.destroy(indices);
    }
  }
  GpuResources &resources;
  BufferHandle vertices;
  BufferHandle indices;
  std::uint32_t vertexCount = 0;
  std::uint32_t indexCount = 0;
  std::size_t bytes = 0;
  std::uint64_t lastPresentation = 0;
};

struct OwnerChunk {
  std::shared_ptr<const PreparedTerrainChunkLod> descriptor;
  std::vector<const Entity *> surfaces;
  std::map<std::string, std::size_t> cameraLevels;
};

struct OwnerProfile {
  bool compatible = false;
  std::string reason;
  const Entity *owner = nullptr;
  std::vector<OwnerChunk> chunks;
};

bool canonicalAppearance(const Entity &entity,
                         const TerrainRenderLod &terrain) {
  const auto *mesh = entity.component<MeshRendererComponent>();
  const auto *transform = entity.component<Transform3DComponent>();
  if (!mesh || !transform || !terrain.chunk || !terrain.chunk->field ||
      terrain.fullGeometryRevision == 0 ||
      mesh->revision != terrain.fullGeometryRevision) {
    return false;
  }
  const auto &field = *terrain.chunk->field;
  const auto biome =
      std::find(field.biomeIds.begin(), field.biomeIds.end(), terrain.biome);
  if (biome == field.biomeIds.end()) {
    return false;
  }
  const auto index = std::size_t(biome - field.biomeIds.begin());
  if (index >= field.biomeColors.size()) {
    return false;
  }
  const auto color = field.biomeColors[index];
  const bool sameColor = mesh->color.r == color.r && mesh->color.g == color.g &&
                         mesh->color.b == color.b && mesh->color.a == color.a;
  const bool ordinaryTransform =
      transform->parent == terrain.owner && transform->position.x == 0 &&
      transform->position.y == 0 && transform->position.z == 0 &&
      transform->rotation.x == 0 && transform->rotation.y == 0 &&
      transform->rotation.z == 0 && transform->scale.x == 1 &&
      transform->scale.y == 1 && transform->scale.z == 1;
  const bool ordinarySize =
      mesh->size.x == 1 && mesh->size.y == 1 && mesh->size.z == 1;
  const bool ordinarySurface =
      mesh->model.empty() && mesh->texture.empty() &&
      mesh->material == field.biomeMaterial(index) &&
      mesh->vertexColors.empty() && mesh->materialNumbers.empty() &&
      mesh->materialColors.empty() && !mesh->metallic && !mesh->roughness &&
      !mesh->opacity && !mesh->surfaceMode;
  return sameColor && ordinaryTransform && ordinarySize && ordinarySurface &&
         mesh->renderLayer.empty() && !mesh->wireframe &&
         mesh->cullDistance == 0 && mesh->mediumLodModel.empty() &&
         mesh->lowLodModel.empty() && mesh->mediumLodDistance == 0 &&
         mesh->lowLodDistance == 0 &&
         !entity.component<MeshInstances3DComponent>() &&
         !entity.component<AnimationPlayer3DComponent>() &&
         !entity.component<SurfaceRelief3DComponent>() &&
         !entity.component<FragmentOpacity3D>() &&
         entityMeshDents3D(entity).empty();
}

std::vector<VisibleMesh3D> fullFallback(const OwnerChunk &chunk,
                                        const WorldTransform3D &transform) {
  std::vector<VisibleMesh3D> draws;
  draws.reserve(chunk.surfaces.size());
  for (const auto *entity : chunk.surfaces) {
    draws.push_back(
        {.entity = entity, .transform = transform, .terrainSurface = true});
  }
  return draws;
}

} // namespace

struct TerrainPatchBatch3D::Impl {
  explicit Impl(GpuResources &owner) : resources(owner) {}
  GpuResources &resources;
  EntityLookup lookup;
  std::uint64_t presentation = 0;
  const World *profileWorld = nullptr;
  std::unordered_map<std::string, OwnerProfile> owners;
  std::unordered_set<std::string> suppressed;
  std::unordered_set<std::string> fullOnly;
  std::map<TerrainPatchTopologyKey3D, std::shared_ptr<GpuTopology>> topologies;
  struct Patch {
    std::shared_ptr<const TerrainGpuField3D> field;
    std::shared_ptr<GpuTopology> topology;
    TerrainPatchInstance3D instance;
    std::vector<VisibleMesh3D> fallback;
    std::size_t level = 0;
    std::shared_ptr<const TerrainMaterialArrays3D> arrays;
    float detailDistance = 200;
  };
  std::vector<Patch> patches;
  std::vector<VisibleMesh3D> immediateFallback;
  TerrainPatchStatistics3D statistics;
  UniformHandle fieldUniform;
  UniformHandle paletteUniform;
  std::array<SamplerHandle, 9> samplers{};
  UniformHandle surfaceUniform;

  bool bindingsReady(std::string &error) {
    if (!fieldUniform) {
      fieldUniform = resources.createUniform("u_terrainField",
                                             UniformType::Vec4, 1, error);
    }
    if (!paletteUniform) {
      paletteUniform = resources.createUniform("u_terrainPalette",
                                               UniformType::Vec4, 1, error);
    }
    if (!surfaceUniform) {
      surfaceUniform = resources.createUniform("u_terrainSurface",
                                               UniformType::Vec4, 1, error);
    }
    constexpr std::array names{
        "s_terrainHeight",    "s_terrainNormal", "s_terrainIds",
        "s_terrainPalette",   "s_terrainAlbedo", "s_terrainNormals",
        "s_terrainOrmHeight", "s_terrainDetail", "s_terrainEmissive"};
    for (std::size_t index = 0; index < samplers.size(); ++index) {
      if (!samplers[index]) {
        samplers[index] = resources.createSampler(names[index], error);
      }
    }
    return fieldUniform && paletteUniform && surfaceUniform &&
           std::ranges::all_of(
               samplers, [](const auto sampler) { return bool(sampler); });
  }

  std::size_t topologyBytes() const {
    std::size_t bytes = 0;
    for (const auto &[key, topology] : topologies) {
      bytes += topology->bytes;
    }
    return bytes;
  }

  std::shared_ptr<GpuTopology> topology(const TerrainPatchTopologyKey3D &key,
                                        std::size_t budget,
                                        std::string &error) {
    if (const auto found = topologies.find(key); found != topologies.end()) {
      const auto cached = found->second;
      if (topologyBytes() > budget) {
        std::erase_if(topologies, [&](const auto &entry) {
          return entry.first != key &&
                 entry.second->lastPresentation != presentation;
        });
      }
      if (topologyBytes() > budget) {
        if (cached->lastPresentation != presentation) {
          topologies.erase(key);
        }
        error =
            "Cached terrain topology exceeds the configured GPU cache budget";
        return {};
      }
      cached->lastPresentation = presentation;
      return cached;
    }
    PrimitiveMeshData3D data;
    try {
      data = createTerrainPatchTopology3D(key);
    } catch (const std::exception &failure) {
      error = failure.what();
      return {};
    }
    const auto bytes = data.positions.size() * sizeof(Vec3) +
                       data.indices.size() * sizeof(std::uint32_t);
    auto current = topologyBytes();
    if (bytes > budget || current > budget - bytes) {
      std::erase_if(topologies, [this](const auto &entry) {
        return entry.second->lastPresentation != presentation;
      });
      current = topologyBytes();
    }
    if (bytes > budget || current > budget - bytes) {
      error = "Terrain patch topology exceeds the configured GPU cache budget";
      return {};
    }
    auto result = std::make_shared<GpuTopology>(resources);
    result->vertices = resources.createBuffer(
        {.kind = BufferKind::Vertex,
         .data = std::as_bytes(std::span(data.positions)),
         .vertexLayout = {.attributes = {{.semantic = VertexSemantic::Position,
                                          .components = 3,
                                          .type = VertexElementType::Float}}},
         .debugName = "Shared terrain patch samples"},
        error);
    if (!result->vertices) {
      return {};
    }
    result->indices =
        resources.createBuffer({.kind = BufferKind::Index32,
                                .data = std::as_bytes(std::span(data.indices)),
                                .debugName = "Shared terrain patch triangles"},
                               error);
    if (!result->indices) {
      return {};
    }
    result->vertexCount = static_cast<std::uint32_t>(data.positions.size());
    result->indexCount = static_cast<std::uint32_t>(data.indices.size());
    result->bytes = bytes;
    result->lastPresentation = presentation;
    topologies.emplace(key, result);
    return result;
  }

  OwnerProfile &ownerProfile(const World &world,
                             const TerrainRuntimeOwner &registration) {
    auto [entry, inserted] = owners.try_emplace(registration.id);
    if (!inserted) {
      return entry->second;
    }
    auto &profile = entry->second;
    profile.owner = lookup.find(world.entities, registration.id);
    if (!profile.owner) {
      profile.reason = "Terrain placement owner is unavailable";
      return profile;
    }
    std::unordered_map<const PreparedTerrainChunkLod *, std::size_t>
        chunkIndices;
    for (const auto &id : registration.surfaces) {
      const auto *entity = lookup.find(world.entities, id);
      const auto *terrain =
          entity ? entity->component<TerrainRenderLod>() : nullptr;
      if (!terrain) {
        continue;
      }
      if (!entity->enabled || !canonicalAppearance(*entity, *terrain)) {
        profile.reason =
            "Terrain placement has noncanonical mesh/appearance state";
        return profile;
      }
      auto [chunk, first] =
          chunkIndices.try_emplace(terrain->chunk.get(), profile.chunks.size());
      if (first) {
        profile.chunks.push_back({.descriptor = terrain->chunk});
      }
      profile.chunks[chunk->second].surfaces.push_back(entity);
    }
    for (const auto &chunk : profile.chunks) {
      std::size_t triangles = 0;
      for (const auto *entity : chunk.surfaces) {
        triangles +=
            entity->component<MeshRendererComponent>()->vertices.size() / 3;
      }
      if (triangles != chunk.descriptor->triangleCounts[0]) {
        profile.reason = "Terrain chunk no longer has complete canonical Full "
                         "surface coverage";
        return profile;
      }
    }
    profile.compatible = !profile.chunks.empty();
    return profile;
  }
};

TerrainPatchBatch3D::TerrainPatchBatch3D(GpuResources &resources)
    : impl_(std::make_unique<Impl>(resources)) {}

TerrainPatchBatch3D::~TerrainPatchBatch3D() {
  clear();
  for (const auto sampler : impl_->samplers) {
    if (sampler) {
      impl_->resources.destroy(sampler);
    }
  }
  if (impl_->fieldUniform) {
    impl_->resources.destroy(impl_->fieldUniform);
  }
  if (impl_->surfaceUniform) {
    impl_->resources.destroy(impl_->surfaceUniform);
  }
  if (impl_->paletteUniform) {
    impl_->resources.destroy(impl_->paletteUniform);
  }
}

void TerrainPatchBatch3D::beginPresentation() {
  ++impl_->presentation;
  impl_->owners.clear();
  impl_->profileWorld = nullptr;
  std::erase_if(impl_->topologies, [this](const auto &entry) {
    return impl_->presentation - entry.second->lastPresentation > 8;
  });
}

void TerrainPatchBatch3D::beginView() {
  impl_->patches.clear();
  impl_->immediateFallback.clear();
  impl_->suppressed.clear();
  impl_->fullOnly.clear();
  impl_->statistics = {};
}

void TerrainPatchBatch3D::clear() {
  beginView();
  impl_->topologies.clear();
  impl_->owners.clear();
  impl_->lookup.clear();
}

void TerrainPatchBatch3D::prepareExtraction(
    const World &world, const BgfxCameraFrame3D &view,
    const BgfxCameraFrame3D &lodCamera, TerrainDrawSelection3D &selection,
    TerrainGpuFieldCache3D &fields, const MaterialLibrary &materials,
    TerrainCameraDemand3D &demand, std::size_t gpuBudgetBytes,
    bool programsReady, TerrainMaterialResidency3D *materialResidency,
    float detailDistance, bool mappedProgramsReady) {
  if (impl_->profileWorld != &world) {
    impl_->owners.clear();
    impl_->profileWorld = &world;
  }
  for (const auto &registration : world.terrainOwners) {
    auto &profile = impl_->ownerProfile(world, registration);
    if (!profile.compatible) {
      impl_->statistics.fallbackReason = profile.reason;
      continue;
    }
    const auto transform = resolveWorldTransform3D(world, *profile.owner);
    const auto *quality = profile.owner->component<Terrain3DComponent>();
    if (!transform || !quality) {
      continue;
    }
    std::vector<OwnerChunk *> visible;
    std::size_t culled = 0;
    for (auto &chunk : profile.chunks) {
      if (!terrainChunkVisible3D(*chunk.descriptor, *transform, view)) {
        ++culled;
        continue;
      }
      visible.push_back(&chunk);
    }
    if (visible.empty()) {
      continue;
    }
    if (!programsReady || gpuBudgetBytes == 0) {
      impl_->statistics.fallbackPatches += visible.size();
      impl_->statistics.fallbackReason =
          gpuBudgetBytes == 0
              ? "GPU terrain cache is disabled"
              : "GPU terrain programs/capabilities are unavailable";
      continue;
    }
    const auto source = quality->generated
                            ? quality->generated
                            : profile.chunks.front().descriptor->field;
    const auto field = fields.request(source, materials);
    if (!field.field) {
      impl_->statistics.fallbackPatches += visible.size();
      impl_->statistics.fallbackReason = field.fallbackReason;
      if (field.preparing) {
        impl_->fullOnly.insert(registration.id);
        for (const auto *chunk : visible) {
          demand.record(lodCamera, chunk->surfaces.front()->id, registration.id,
                        chunk->descriptor, 0, true);
        }
      }
      continue;
    }
    std::shared_ptr<const TerrainMaterialArrays3D> arrays;
    if (field.field->mapped) {
      if (!mappedProgramsReady || !materialResidency) {
        impl_->statistics.fallbackPatches += visible.size();
        impl_->statistics.fallbackReason =
            "Mapped terrain programs are unavailable";
        continue;
      }
      const auto localCamera =
          inverseTransformPoint3D(*transform, lodCamera.position);
      float nearestDistance = std::numeric_limits<float>::max();
      const float minimumScale =
          std::min({std::abs(transform->scale.x), std::abs(transform->scale.y),
                    std::abs(transform->scale.z)});
      for (const auto *chunk : visible) {
        const auto &bounds = *chunk->descriptor;
        const float dx = std::max({bounds.boundsMin.x - localCamera.x, 0.F,
                                   localCamera.x - bounds.boundsMax.x});
        const float dy = std::max({bounds.boundsMin.y - localCamera.y, 0.F,
                                   localCamera.y - bounds.boundsMax.y});
        const float dz = std::max({bounds.boundsMin.z - localCamera.z, 0.F,
                                   localCamera.z - bounds.boundsMax.z});
        nearestDistance =
            std::min(nearestDistance,
                     std::sqrt(dx * dx + dy * dy + dz * dz) * minimumScale);
      }
      const auto request =
          materialResidency->request(*field.field, nearestDistance);
      arrays = request.arrays;
      if (!arrays) {
        impl_->statistics.fallbackPatches += visible.size();
        impl_->statistics.fallbackReason = request.fallbackReason;
        continue;
      }
    }
    impl_->statistics.consideredPatches += profile.chunks.size();
    impl_->statistics.culledPatches += culled;
    impl_->suppressed.insert(registration.id);
    const auto viewDepth = [&](const OwnerChunk *chunk) {
      const auto &bounds = *chunk->descriptor;
      const Vec3 localCenter{(bounds.boundsMin.x + bounds.boundsMax.x) * 0.5F,
                             (bounds.boundsMin.y + bounds.boundsMax.y) * 0.5F,
                             (bounds.boundsMin.z + bounds.boundsMax.z) * 0.5F};
      const auto center = transformPoint3D(*transform, localCenter);
      return (center.x - view.position.x) * view.forward.x +
             (center.y - view.position.y) * view.forward.y +
             (center.z - view.position.z) * view.forward.z;
    };
    // Instance order survives topology grouping. Nearer opaque patches populate
    // depth first so hidden terrain can skip expensive mapped surface shading.
    // Shadow views use their own direction; LOD still belongs to lodCamera.
    std::stable_sort(visible.begin(), visible.end(),
                     [&](const OwnerChunk *first, const OwnerChunk *second) {
                       return viewDepth(first) < viewDepth(second);
                     });
    for (auto *chunk : visible) {
      const auto key = lodCamera.cameraId.empty()
                           ? "view:" + std::to_string(lodCamera.viewId)
                           : lodCamera.cameraId;
      auto [level, first] = chunk->cameraLevels.try_emplace(key);
      if (first) {
        level->second = selection.select(
            chunk->descriptor, *transform, {1, 1, 1}, lodCamera,
            quality->lodErrorPixels, registration.id);
      }
      demand.record(lodCamera, chunk->surfaces.front()->id, registration.id,
                    chunk->descriptor, level->second, true);
      const auto &rectangle = chunk->descriptor->rectangle;
      const TerrainPatchTopologyKey3D topology{
          rectangle.cellsX, rectangle.cellsZ,
          static_cast<TerrainLod>(level->second)};
      TerrainPatchInstance3D instance{
          .transform = composeMeshTransform3D(*transform, {1, 1, 1}),
          .gridOrigin = {float(rectangle.firstCellX),
                         float(rectangle.firstCellZ),
                         quality->surfaceBlending ? 1.F : 0.F, 0}};
      auto fallback = fullFallback(*chunk, *transform);
      std::string error;
      const auto textureBytes = fields.residentBytes();
      const auto topologyBudget =
          gpuBudgetBytes > textureBytes ? gpuBudgetBytes - textureBytes : 0;
      if (!addPatch(field.field, topology, instance, fallback, topologyBudget,
                    error, arrays, detailDistance)) {
        impl_->statistics.fallbackReason = error;
        ++impl_->statistics.fallbackPatches;
        impl_->immediateFallback.insert(impl_->immediateFallback.end(),
                                        fallback.begin(), fallback.end());
      }
    }
  }
}

bool TerrainPatchBatch3D::suppressesOwner(const std::string &owner) const {
  return impl_->suppressed.contains(owner);
}

bool TerrainPatchBatch3D::forcesFullOwner(const std::string &owner) const {
  return impl_->fullOnly.contains(owner);
}

bool TerrainPatchBatch3D::addPatch(
    std::shared_ptr<const TerrainGpuField3D> field,
    TerrainPatchTopologyKey3D topology, const TerrainPatchInstance3D &instance,
    std::vector<VisibleMesh3D> fallbackMeshes, std::size_t gpuBudgetBytes,
    std::string &error, std::shared_ptr<const TerrainMaterialArrays3D> arrays,
    float detailDistance) {
  if (!field) {
    error = "GPU terrain patches require a ready field binding";
    return false;
  }
  const auto mesh = impl_->topology(topology, gpuBudgetBytes, error);
  if (!mesh) {
    return false;
  }
  impl_->patches.push_back({std::move(field), mesh, instance,
                            std::move(fallbackMeshes),
                            std::size_t(terrainLodIndex(topology.level)),
                            std::move(arrays), detailDistance});
  return true;
}

std::vector<VisibleMesh3D> TerrainPatchBatch3D::flush(
    RenderCommands &commands, std::uint16_t viewId, ProgramHandle program,
    const std::array<DrawUniformValue, 8> &lighting, bool overdraw,
    std::string &error, ProgramHandle mappedProgram,
    ProgramHandle opaqueMappedProgram) {
  auto fallback = std::move(impl_->immediateFallback);
  if (impl_->patches.empty()) {
    return fallback;
  }
  struct Group {
    std::shared_ptr<const TerrainGpuField3D> field;
    std::shared_ptr<GpuTopology> mesh;
    std::shared_ptr<const TerrainMaterialArrays3D> arrays;
    float detailDistance = 200;
    std::vector<std::size_t> patches;
    std::vector<TerrainPatchInstance3D> instances;
  };
  std::vector<Group> groups;
  using Key = std::tuple<const TerrainGpuField3D *, const GpuTopology *,
                         const TerrainMaterialArrays3D *, float>;
  std::map<Key, std::size_t> groupOf;
  for (std::size_t index = 0; index < impl_->patches.size(); ++index) {
    const auto &patch = impl_->patches[index];
    const auto key = Key{patch.field.get(), patch.topology.get(),
                         patch.arrays.get(), patch.detailDistance};
    auto [entry, first] = groupOf.try_emplace(key, groups.size());
    if (first) {
      groups.push_back({.field = patch.field,
                        .mesh = patch.topology,
                        .arrays = patch.arrays,
                        .detailDistance = patch.detailDistance});
    }
    auto &group = groups[entry->second];
    group.patches.push_back(index);
    group.instances.push_back(patch.instance);
  }
  const bool ready = impl_->bindingsReady(error);
  for (const auto &group : groups) {
    std::size_t submitted = 0;
    const auto selectedMappedProgram =
        !group.field->requiresFragmentCulling && opaqueMappedProgram
            ? opaqueMappedProgram
            : mappedProgram;
    if (ready && (!group.arrays || selectedMappedProgram)) {
      auto state = group.field->state;
      if (overdraw) {
        state.blend = BlendMode::Additive;
        state.depthTest = DepthTest::Always;
        state.writeDepth = false;
      }
      std::array<DrawUniformValue, 11> uniforms;
      std::copy(lighting.begin(), lighting.end(), uniforms.begin());
      uniforms[8] = {.handle = impl_->fieldUniform,
                     .values = group.field->fieldParameters};
      uniforms[9] = {.handle = impl_->paletteUniform,
                     .values = group.field->paletteParameters};
      const std::array<float, 4> surfaceParameters{
          group.arrays ? float(group.arrays->edge) : 1.F, group.detailDistance,
          0, 0};
      uniforms[10] = {.handle = impl_->surfaceUniform,
                      .values = surfaceParameters};
      std::vector<DrawTextureBinding> textures{
          DrawTextureBinding{.stage = 3,
                             .texture = group.field->height,
                             .sampler = impl_->samplers[0]},
          DrawTextureBinding{.stage = 4,
                             .texture = group.field->normal,
                             .sampler = impl_->samplers[1]},
          DrawTextureBinding{.stage = 5,
                             .texture = group.field->classification,
                             .sampler = impl_->samplers[2]},
          DrawTextureBinding{.stage = 6,
                             .texture = group.field->palette,
                             .sampler = impl_->samplers[3]}};
      if (group.arrays) {
        for (std::size_t slot = 0; slot < group.arrays->textures.size();
             ++slot) {
          textures.push_back({.stage = static_cast<std::uint8_t>(7 + slot),
                              .texture = group.arrays->textures[slot],
                              .sampler = impl_->samplers[4 + slot]});
        }
      }
      const auto result = commands.submitInstanceBatches(
          {.viewId = viewId,
           .vertices = {.handle = group.mesh->vertices,
                        .count = group.mesh->vertexCount},
           .indices = {.handle = group.mesh->indices,
                       .count = group.mesh->indexCount},
           .program = group.arrays ? selectedMappedProgram : program,
           .state = state,
           .instances = instanceData(std::span(group.instances)),
           .uniforms = uniforms,
           .textures = textures},
          error);
      submitted = result.submittedRecords;
      impl_->statistics.draws += result.submittedBatches;
      impl_->statistics.instances += submitted;
      impl_->statistics.triangles += submitted * (group.mesh->indexCount / 3U);
    }
    for (std::size_t index = 0; index < submitted; ++index) {
      ++impl_->statistics
            .selectedLevels[impl_->patches[group.patches[index]].level];
    }
    for (std::size_t index = submitted; index < group.patches.size(); ++index) {
      const auto &patch = impl_->patches[group.patches[index]];
      fallback.insert(fallback.end(), patch.fallback.begin(),
                      patch.fallback.end());
      ++impl_->statistics.fallbackPatches;
    }
    if (submitted < group.patches.size()) {
      impl_->statistics.fallbackReason = error;
    }
  }
  // Optional terrain submission failures have exact Full fallback coverage.
  // Clear their transient error before the ordinary renderer handles that data.
  error.clear();
  impl_->patches.clear();
  return fallback;
}

const TerrainPatchStatistics3D &TerrainPatchBatch3D::statistics() const {
  return impl_->statistics;
}

std::size_t TerrainPatchBatch3D::topologyBytes() const {
  return impl_->topologyBytes();
}

} // namespace demi::runtime::render
