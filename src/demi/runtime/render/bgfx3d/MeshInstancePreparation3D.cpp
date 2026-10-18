#include "demi/runtime/render/bgfx3d/MeshInstancePreparation3D.h"
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
  sources.resize(world.entities.size());
  for (std::size_t index = 0; index < world.entities.size(); ++index) {
    sources[index].entity = &world.entities[index];
    sources[index].transform = transforms[index];
  }

  // Membership survives presentations; borrowed pointers and values do not.
  // Sparse components need lookups only on their actual owners.
  const auto populateComponents = [&]<typename Component>(
                                      const Component *EntitySource::*member,
                                      bool requireMesh = false) {
    const auto &indices =
        sourceMembership_.queryAny<Component>(world.entities);
    for (const auto index : indices) {
      auto &source = sources[index];
      if (!requireMesh || source.mesh) {
        source.*member = world.entities[index].template component<Component>();
      }
    }
  };
  populateComponents(&EntitySource::mesh);
  populateComponents(&EntitySource::instances);
  populateComponents(&EntitySource::sections);
  populateComponents(&EntitySource::terrain);
  populateComponents(&EntitySource::animation, true);
  populateComponents(&EntitySource::fade, true);
  populateComponents(&EntitySource::relief, true);
  for (const auto index :
       sourceMembership_.queryAny<Dentable3DComponent>(world.entities)) {
    auto &source = sources[index];
    if (source.mesh) {
      source.dents = world.entities[index].component<Dentable3DComponent>()->dents;
    }
  }

  bool hasAnimation = false;
  for (auto &source : sources) {
    hasAnimation |= source.animation != nullptr;
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
    }
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
    ++index;
  }
  return prepared;
}
} // namespace demi::runtime::render
