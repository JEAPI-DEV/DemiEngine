#include "demi/runtime/render/bgfx3d/MeshInstancePreparation3D.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"

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

void MeshInstancePreparation3D::reset() { sources_.clear(); }

MeshInstancePreparation3D::Source &
MeshInstancePreparation3D::source(const World &world, const Entity &entity,
                                  const WorldTransform3D &owner,
                                  const MeshInstances3DComponent &instances) {
  auto [entry, created] = sources_[&world].try_emplace(&entity);
  auto &prepared = entry->second;
  if (!created && prepared.component == &instances &&
      prepared.copies.size() == instances.transforms.size() &&
      sameTransform(prepared.owner, owner)) {
    return prepared;
  }
  prepared = Source{};
  prepared.owner = owner;
  prepared.component = &instances;
  prepared.copies.reserve(instances.transforms.size());
  const auto basisX = transformDirection3D(owner, {owner.scale.x, 0, 0});
  const auto basisY = transformDirection3D(owner, {0, owner.scale.y, 0});
  const auto basisZ = transformDirection3D(owner, {0, 0, owner.scale.z});
  for (const auto &[id, local] : instances.transforms) {
    const auto point = local.position;
    const Vec3 position{owner.position.x + basisX.x * point.x +
                            basisY.x * point.y + basisZ.x * point.z,
                        owner.position.y + basisX.y * point.x +
                            basisY.y * point.y + basisZ.y * point.z,
                        owner.position.z + basisX.z * point.x +
                            basisY.z * point.y + basisZ.z * point.z};
    prepared.copies.push_back({.local = local, .position = position});
  }
  return prepared;
}
} // namespace demi::runtime::render
