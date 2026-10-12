#include "demi/runtime/render/bgfx3d/TerrainGpuMeshCache3D.h"

#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"

#include <algorithm>
#include <functional>

namespace demi::runtime::render {

void TerrainGpuMeshCache3D::beginPresentation() { ++presentation_; }

void TerrainGpuMeshCache3D::endPresentation(bool succeeded) {
  if (!succeeded) {
    return;
  }
  const auto unused = [this](const auto &item) {
    const auto &entry = item.second;
    return !entry || presentation_ - entry->lastPresentation > 8;
  };
  std::erase_if(fullSurfaces_, unused);
  std::erase_if(variants_, unused);
}

void TerrainGpuMeshCache3D::clear() {
  fullSurfaces_.clear();
  variants_.clear();
  presentation_ = 0;
  uploads_ = 0;
}

const GpuMesh3D *TerrainGpuMeshCache3D::get(
    const std::string &surfaceId, std::size_t level,
    const MeshRendererComponent &geometry,
    std::shared_ptr<const MeshRendererComponent> retainedGeometry,
    const MeshMaterialSections3D *sections, std::string &error) {
  if (level >= 4 || (level != 0 && !retainedGeometry)) {
    error = "Terrain GPU cache requires a valid level and retained reduced "
            "geometry";
    return nullptr;
  }
  const auto indexRevision =
      sections && sections->indices ? sections->indexRevision : 0;
  auto &entry =
      level == 0 ? fullSurfaces_[surfaceId]
                 : variants_[VariantKey{retainedGeometry.get(), indexRevision}];
  const bool sameSource = entry && entry->geometry.lock() == retainedGeometry &&
                          entry->geometryRevision == geometry.revision &&
                          entry->indexRevision == indexRevision;
  if (!sameSource) {
    if (!geometry.validateVertexColors(error)) {
      return nullptr;
    }
    std::vector<std::uint32_t> colors;
    colors.reserve(geometry.vertexColors.size());
    for (const auto &color : geometry.vertexColors) {
      colors.push_back(packVertexColorRgba8(color));
    }
    std::span<const std::uint32_t> indices;
    if (sections && sections->indices) {
      indices = *sections->indices;
    }
    auto replacement = std::make_unique<Entry>(resources_);
    if (!replacement->mesh.upload(geometry.vertices, geometry.uvs, indices,
                                  0xffffffffU, error, geometry.normals,
                                  colors)) {
      return nullptr;
    }
    replacement->geometry = std::move(retainedGeometry);
    replacement->geometryRevision = geometry.revision;
    replacement->indexRevision = indexRevision;
    replacement->bytes =
        replacement->mesh.vertexCount() * sizeof(GpuMeshVertex3D) +
        replacement->mesh.indexCount() * sizeof(std::uint32_t);
    entry = std::move(replacement);
    ++uploads_;
  }
  entry->lastPresentation = presentation_;
  return &entry->mesh;
}

std::size_t
TerrainGpuMeshCache3D::VariantHash::operator()(const VariantKey &key) const {
  const auto identity =
      std::hash<const MeshRendererComponent *>{}(key.geometry);
  return identity ^ (std::hash<std::uint64_t>{}(key.indexRevision) +
                     0x9e3779b9U + (identity << 6U) + (identity >> 2U));
}

std::size_t TerrainGpuMeshCache3D::residentMeshes() const {
  std::size_t count = 0;
  for (const auto &[id, entry] : fullSurfaces_) {
    count += entry ? 1U : 0U;
  }
  for (const auto &[key, entry] : variants_) {
    count += entry ? 1U : 0U;
  }
  return count;
}

std::size_t TerrainGpuMeshCache3D::residentBytes() const {
  std::size_t bytes = 0;
  for (const auto &[id, entry] : fullSurfaces_) {
    bytes += entry ? entry->bytes : 0;
  }
  for (const auto &[key, entry] : variants_) {
    bytes += entry ? entry->bytes : 0;
  }
  return bytes;
}

} // namespace demi::runtime::render
