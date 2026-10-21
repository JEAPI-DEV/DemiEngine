#include "demi/runtime/render/bgfx3d/MeshInstancePreparation3D.h"
#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/destruction/DetachedFragmentFade3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/scene/WorldTransformSnapshot3D.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Dentable3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/SurfaceRelief3DComponent.h"
#include "demi/runtime/scene/model/MeshMaterialSections3D.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainRenderLod.h"
#include <algorithm>
#include <cmath>

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
  std::erase_if(worldSources_,
                [](const auto &entry) { return !entry.second.visited; });
  for (auto &[world, sources] : worldSources_) {
    sources.prepared = false;
    sources.visited = false;
  }
  // Retain only sources used in the last presentation. Cached copy transforms
  // own their values; component pointers and geometry decisions expire here.
  for (auto &[world, sources] : sources_) {
    std::erase_if(sources,
                  [](const auto &entry) { return !entry.second.visited; });
    for (auto &[entity, source] : sources) {
      source.visited = false;
      source.component = nullptr;
      source.geometryPrepared = false;
    }
  }
  std::erase_if(sources_,
                [](const auto &entry) { return entry.second.empty(); });
  animations_.clear();
}

std::vector<MeshInstancePreparation3D::EntitySource> &
MeshInstancePreparation3D::worldSources(const World &world, JobSystem *jobs) {
  auto &cached = worldSources_[&world];
  auto &sources = cached.entries;
  const auto &registry = world.entities.componentRegistry();
  const auto revision = registry.storageRevision();
  const bool refreshAddresses =
      cached.orderRevision != world.entities.orderRevision() ||
      cached.storageRevision != revision ||
      cached.registryIdentity != registry.identity() ||
      cached.entityData != world.entities.data() ||
      sources.size() != world.entities.size();
  cached.visited = true;
  if (cached.prepared && !refreshAddresses) {
    return sources;
  }
  ProfileScope prepareScope("Renderer3D.prepare_sources");
  RuntimeProfiler::setGauge("Renderer3D.source_addresses_rebuilt",
                            refreshAddresses ? 1.0 : 0.0);
  RuntimeProfiler::setGauge("Renderer3D.component_storage_revision", revision);
  RuntimeProfiler::setGauge("Renderer3D.entity_order_revision",
                            world.entities.orderRevision());
  const auto &transforms = transformSnapshot_.refresh(world, jobs);
  sources.resize(world.entities.size());
  for (std::size_t index = 0; index < world.entities.size(); ++index) {
    if (refreshAddresses) {
      sources[index] = EntitySource{};
    }
    sources[index].entity = &world.entities[index];
    sources[index].transformUnchanged =
        sources[index].transform && transforms[index] &&
        sameTransform(*sources[index].transform, *transforms[index]);
    sources[index].transform = transforms[index];
    if (!sources[index].transformUnchanged) {
      sources[index].modelMatrix.reset();
    }
    sources[index].transparent.reset();
    sources[index].sectionTransparency.clear();
  }

  // Reacquire addresses after storage changes, including same-type replacement.
  // Sparse components need lookups only on their actual owners.
  const auto populateComponents = [&]<typename Component>(
                                      const Component *EntitySource::*member,
                                      bool requireMesh = false) {
    const auto &indices = sourceMembership_.queryAny<Component>(world.entities);
    for (const auto index : indices) {
      auto &source = sources[index];
      if (!requireMesh || source.mesh) {
        source.*member = world.entities[index].template component<Component>();
      }
    }
  };
  if (refreshAddresses) {
    populateComponents(&EntitySource::mesh);
    populateComponents(&EntitySource::instances);
    populateComponents(&EntitySource::sections);
    populateComponents(&EntitySource::terrain);
    populateComponents(&EntitySource::animation, true);
    populateComponents(&EntitySource::fade, true);
    populateComponents(&EntitySource::relief, true);
    populateComponents(&EntitySource::dentable, true);
    cached.storageRevision = revision;
    cached.registryIdentity = registry.identity();
    cached.orderRevision = world.entities.orderRevision();
    cached.entityData = world.entities.data();
  }

  const auto refreshSource = [&](std::size_t index) {
    auto &source = sources[index];
    source.dents = source.dentable
                       ? std::span<const MeshDent3D>(source.dentable->dents)
                       : std::span<const MeshDent3D>{};
    const bool wasPlainPrimitive = source.plainPrimitive;
    if (const auto *mesh = source.mesh) {
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
      const bool sameSize = sameVector(source.preparedMeshSize, mesh->size);
      if (!sameSize) {
        source.modelMatrix.reset();
      }
      source.preparedMeshSize = mesh->size;
      const MeshBounds3D bounds =
          mesh->hasBounds ? MeshBounds3D{mesh->boundsMin, mesh->boundsMax}
                          : MeshBounds3D{{-.5F, -.5F, -.5F}, {.5F, .5F, .5F}};
      const bool sameBounds =
          sameVector(source.primitiveBounds.minimum, bounds.minimum) &&
          sameVector(source.primitiveBounds.maximum, bounds.maximum);
      // Plain primitives have no asset-dependent bounds, animation, dents or
      // material wind. All inputs to their world sphere are checked here.
      source.geometryPrepared = source.geometryPrepared && wasPlainPrimitive &&
                                source.plainPrimitive && !source.instances &&
                                source.transformUnchanged && sameSize &&
                                sameBounds;
      source.primitiveBounds = bounds;
    } else {
      source.geometryPrepared = false;
    }
  };
  if (jobs && sources.size() >= 1024) {
    jobs->parallelFor(sources.size(), 256, refreshSource);
  } else {
    for (std::size_t index = 0; index < sources.size(); ++index) {
      refreshSource(index);
    }
  }
  animations_[&world] =
      std::any_of(sources.begin(), sources.end(), [](const auto &source) {
        return source.animation != nullptr;
      });
  cached.prepared = true;
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
  if (!created && prepared.contentRevision == instances.contentRevision() &&
      sameTransform(prepared.owner, owner)) {
    prepared.component = &instances;
    prepared.visited = true;
    return prepared;
  }
  ProfileScope scope("Renderer3D.prepare_instance_copies");
  const bool ownerChanged = created || !sameTransform(prepared.owner, owner);
  prepared.owner = owner;
  prepared.component = &instances;
  prepared.contentRevision = instances.contentRevision();
  prepared.visited = true;
  prepared.geometryPrepared = false;
  const auto oldCount = prepared.copies.size();
  prepared.copies.resize(instances.transforms().size());
  const auto basis = transformBasis3D(owner);
  const auto &basisX = basis.right;
  const auto &basisY = basis.up;
  const auto &basisZ = basis.forward;
  std::size_t index = 0;
  for (const auto &[id, local] : instances.transforms()) {
    auto &copy = prepared.copies[index];
    const bool changed =
        ownerChanged || index >= oldCount || !sameTransform(copy.local, local);
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
    ++index;
  }
  prepared.spatialIndex.rebuild(
      prepared.copies.size(),
      [&](std::size_t index) { return prepared.copies[index].position; },
      [&](std::size_t index) {
        const auto scale = prepared.copies[index].local.scale;
        return std::max({std::abs(owner.scale.x * scale.x),
                         std::abs(owner.scale.y * scale.y),
                         std::abs(owner.scale.z * scale.z)});
      });
  return prepared;
}
} // namespace demi::runtime::render
