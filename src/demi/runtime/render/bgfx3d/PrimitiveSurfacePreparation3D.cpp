#include "demi/runtime/render/bgfx3d/PrimitiveSurfacePreparation3D.h"
#include "demi/runtime/render/bgfx3d/PreparedMeshDraw3D.h"
#include <bit>

namespace demi::runtime::render {
void PrimitiveSurfacePreparation3D::reset() {
  groups_.clear();
  sources_.clear();
  surfaces_.clear();
}
std::optional<std::size_t> PrimitiveSurfacePreparation3D::surfaceFor(
    const MeshInstancePreparation3D::EntitySource &source) {
  if (!source.plainPrimitive) {
    return std::nullopt;
  }
  if (const auto cached = sources_.find(&source); cached != sources_.end()) {
    return cached->second;
  }
  const auto &mesh = *source.mesh;
  const PreparedMeshDraw3D prepared(mesh, nullptr, std::nullopt, false, false,
                                    false, {});
  std::array<std::uint32_t, 8> values;
  for (std::size_t index = 0; index < 4; ++index) {
    values[index] =
        std::bit_cast<std::uint32_t>(prepared.builtinSurfaceTint[index]);
    values[index + 4] =
        std::bit_cast<std::uint32_t>(prepared.metalRough[index]);
  }
  const auto [found, inserted] = groups_.try_emplace(
      Key{mesh.shape, mesh.texture, values}, surfaces_.size());
  if (inserted) {
    surfaces_.push_back({mesh.shape, mesh.texture, prepared.builtinSurfaceTint,
                         prepared.metalRough});
  }
  sources_.emplace(&source, found->second);
  return found->second;
}
} // namespace demi::runtime::render
