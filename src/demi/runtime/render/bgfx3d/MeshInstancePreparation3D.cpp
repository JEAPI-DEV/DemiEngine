#include "demi/runtime/render/bgfx3d/MeshInstancePreparation3D.h"
#include "demi/runtime/destruction/DetachedFragmentFade3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/WorldTransformSnapshot3D.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/SurfaceRelief3DComponent.h"
#include "demi/runtime/scene/model/MeshMaterialSections3D.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainRenderLod.h"
#include <algorithm>

namespace demi::runtime::render {
namespace {
bool sameVector(Vec3 left, Vec3 right) {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}
bool sameTransform(const WorldTransform3D &left,
                   const WorldTransform3D &right) {
  return sameVector(left.position, right.position) &&
         sameVector(left.rotation, right.rotation) &&
         sameVector(left.scale, right.scale);
}
} // namespace

void MeshInstancePreparation3D::reset() {
  spareWorldSources_.clear();
  for (auto &[world, sources] : worldSources_) {
    sources.clear();
    spareWorldSources_.push_back(std::move(sources));
  }
  // Retain only sources used in the last presentation. Cached copy transforms
  // own their values; component pointers and geometry decisions expire here.
  for (auto &[world, sources] : sources_) {
    std::erase_if(sources, [](const auto &entry) {
      return !entry.second.visited;
    });
    for (auto &[entity, source] : sources) {
      source.visited = false;
      source.component = nullptr;
      source.geometryPrepared = false;
    }
  }
  std::erase_if(sources_, [](const auto &entry) {
    return entry.second.empty();
  });
  worldSources_.clear();
  animations_.clear();
}

std::vector<MeshInstancePreparation3D::EntitySource> &
MeshInstancePreparation3D::worldSources(const World &world) {
  auto [entry, created] = worldSources_.try_emplace(&world);
  auto &sources = entry->second;
  if (created && !spareWorldSources_.empty()) {
    sources = std::move(spareWorldSources_.back());
    spareWorldSources_.pop_back();
  }
  if (!created && sources.size() == world.entities.size() &&
      (sources.empty() || sources.front().entity == world.entities.data())) {
    return sources;
  }
  ProfileScope prepareScope("Renderer3D.prepare_sources");
  const auto &transforms = transformSnapshot_.refresh(world);
  sources.clear();
  sources.reserve(world.entities.size());
  bool hasAnimation = false;
  for (std::size_t index = 0; index < world.entities.size(); ++index) {
    const auto &entity = world.entities[index];
    const auto *mesh = entity.component<MeshRendererComponent>();
    EntitySource source{
        .entity = &entity,
        .mesh = mesh,
        .instances = entity.component<MeshInstances3DComponent>(),
        .sections = entity.component<MeshMaterialSections3D>(),
        .terrain = entity.component<TerrainRenderLod>(),
        .animation =
            mesh ? entity.component<AnimationPlayer3DComponent>() : nullptr,
        .fade = mesh ? entity.component<FragmentOpacity3D>() : nullptr,
        .relief = mesh ? entity.component<SurfaceRelief3DComponent>() : nullptr,
        .dents =
            mesh ? entityMeshDents3D(entity) : std::span<const MeshDent3D>{},
        .transform = transforms[index]};
    hasAnimation |= source.animation != nullptr;
    if (mesh) {
      const bool primitive = mesh->shape == "cube" || mesh->shape == "sphere" ||
                             mesh->shape == "cylinder" ||
                             mesh->shape == "plane";
      const bool opaque = !mesh->surfaceMode || *mesh->surfaceMode == "opaque";
      source.plainPrimitive =
          primitive && opaque && mesh->model.empty() &&
          mesh->mediumLodModel.empty() && mesh->lowLodModel.empty() &&
          mesh->vertices.empty() && mesh->vertexColors.empty() &&
          mesh->material.empty() && mesh->materialNumbers.empty() &&
          mesh->materialColors.empty() && !source.animation && !source.fade &&
          !source.relief && !source.sections && !source.terrain &&
          source.dents.empty();
    }
    sources.push_back(source);
  }
  animations_[&world] = hasAnimation;
  return sources;
}

bool MeshInstancePreparation3D::hasAnimations(const World &world) {
  worldSources(world);
  return animations_.at(&world);
}

MeshInstancePreparation3D::Source &
MeshInstancePreparation3D::source(const World &world, const Entity &entity,
                                  const WorldTransform3D &owner,
                                  const MeshInstances3DComponent &instances) {
  auto [entry, created] = sources_[&world].try_emplace(&entity);
  auto &prepared = entry->second;
  if (!created && prepared.visited && prepared.component == &instances &&
      prepared.copies.size() == instances.transforms.size() &&
      sameTransform(prepared.owner, owner)) {
    return prepared;
  }
  ProfileScope scope("Renderer3D.prepare_instance_copies");
  const bool ownerChanged = created || !sameTransform(prepared.owner, owner);
  prepared.owner = owner;
  prepared.component = &instances;
  prepared.visited = true;
  prepared.geometryPrepared = false;
  const auto oldCount = prepared.copies.size();
  prepared.copies.resize(instances.transforms.size());
  const auto basis = transformBasis3D(owner);
  const auto &basisX = basis.right;
  const auto &basisY = basis.up;
  const auto &basisZ = basis.forward;
  std::size_t index = 0;
  for (const auto &[id, local] : instances.transforms) {
    auto &copy = prepared.copies[index];
    const bool changed = ownerChanged || index >= oldCount ||
                         !sameTransform(copy.local, local);
    if (changed) {
      const auto point = local.position;
      const Vec3 position{owner.position.x + basisX.x * point.x +
                              basisY.x * point.y + basisZ.x * point.z,
                          owner.position.y + basisX.y * point.x +
                              basisY.y * point.y + basisZ.y * point.z,
                          owner.position.z + basisX.z * point.x +
                              basisY.z * point.y + basisZ.z * point.z};
      copy = Copy{.local = local, .position = position};
    }
    // Bounds also depend on current model/material data. Recompute those each
    // presentation; unchanged transform/matrix values are safe to retain.
    copy.bounds.reset();
    ++index;
  }
  return prepared;
}
} // namespace demi::runtime::render
