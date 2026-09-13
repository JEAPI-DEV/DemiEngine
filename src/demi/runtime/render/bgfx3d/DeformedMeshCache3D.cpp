#include "demi/runtime/render/bgfx3d/DeformedMeshCache3D.h"

namespace demi::runtime::render {
const GpuMesh3D *DeformedMeshCache3D::get(
    const std::string &entityId, const std::string &sourceId,
    std::uint64_t sourceRevision, std::span<const Vec3> positions,
    std::span<const Vec2> uvs, std::span<const std::uint32_t> indices,
    std::span<const std::uint32_t> colors, std::span<const MeshDent3D> dents,
    std::string &error) {
  const auto signature = meshDentsSignature3D(dents);
  auto &entry = meshes_[entityId];
  if (entry && entry->sourceId == sourceId &&
      entry->sourceRevision == sourceRevision &&
      entry->dentSignature == signature)
    return &entry->gpu;
  MeshGeometry3D deformed;
  if (!refineMeshForDents3D(positions, uvs, indices, colors, dents, deformed,
                            error))
    return nullptr;
  deformMeshPositions3D(deformed.positions, dents);
  // Stage the replacement so allocation/upload failure retains the old buffer.
  auto replacement = std::make_unique<Entry>(resources_);
  if (!replacement->gpu.upload(deformed.positions, deformed.uvs,
                               deformed.indices, 0xffffffffU, error, {},
                               deformed.colors))
    return nullptr;
  replacement->sourceId = sourceId;
  replacement->sourceRevision = sourceRevision;
  replacement->dentSignature = signature;
  entry = std::move(replacement);
  ++uploads_;
  return &entry->gpu;
}

void DeformedMeshCache3D::retain(const std::unordered_set<std::string> &live) {
  std::erase_if(meshes_, [&live](const auto &entry) {
    return !live.contains(entry.first);
  });
}
} // namespace demi::runtime::render
