#include "demi/runtime/render/bgfx3d/DeformedMeshCache3D.h"

namespace demi::runtime::render {
const DeformedMesh3D *DeformedMeshCache3D::get(
    const std::string &entityId, const std::string &sourceId,
    std::uint64_t sourceRevision, std::span<const Vec3> positions,
    std::span<const Vec2> uvs, std::span<const std::uint32_t> indices,
    std::span<const std::uint32_t> colors, std::span<const MeshDent3D> dents,
    std::string &error) {
  const auto signature = meshDentsSignature3D(dents);
  auto &entry = meshes_[entityId];
  if (entry && entry->sourceId == sourceId &&
      entry->sourceRevision == sourceRevision &&
      entry->dentSignature == signature) {
    return &entry->deformed;
  }
  MeshGeometry3D deformed;
  if (!refineMeshForDents3D(positions, uvs, indices, colors, dents, deformed,
                            error)) {
    return nullptr;
  }
  deformMeshPositions3D(deformed.positions, dents);
  // Stage the replacement so allocation/upload failure retains the old buffer.
  auto replacement = std::make_unique<Entry>(resources_);
  if (!replacement->deformed.gpu.upload(deformed.positions, deformed.uvs,
                                        deformed.indices, 0xffffffffU, error,
                                        {}, deformed.colors)) {
    return nullptr;
  }
  const auto sourceIndexCount =
      indices.empty() ? positions.size() : indices.size();
  replacement->deformed.indexMultiplier =
      static_cast<std::uint32_t>(deformed.indices.size() / sourceIndexCount);
  replacement->sourceId = sourceId;
  replacement->sourceRevision = sourceRevision;
  replacement->dentSignature = signature;
  entry = std::move(replacement);
  ++uploads_;
  return &entry->deformed;
}

} // namespace demi::runtime::render
