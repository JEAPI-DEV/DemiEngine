#include "demi/runtime/render/bgfx3d/MeshShadowSections3D.h"

#include "demi/runtime/render/bgfx3d/MeshSurface3D.h"
#include "demi/runtime/render/bgfx3d/VisibleMeshGeometry3D.h"
#include "demi/runtime/scene/model/MeshMaterialSections3D.h"

namespace demi::runtime::render {
namespace {

bool sameShadowState(const DrawState &left, const DrawState &right) {
  return left.depthTest == right.depthTest && left.cull == right.cull &&
         left.topology == right.topology &&
         left.writeColor == right.writeColor &&
         left.writeAlpha == right.writeAlpha &&
         left.multisampling == right.multisampling;
}

bool canDrawWholeShadow(const Entity &entity,
                        const MeshMaterialSections3D &sections,
                        const MaterialLibrary &materials) {
  if (!sections.indices || sections.sections.empty()) {
    return false;
  }
  const auto &mesh = *entity.component<MeshRendererComponent>();
  std::optional<DrawState> sharedState;
  std::size_t nextIndex = 0;
  for (const auto &section : sections.sections) {
    // Native section tables may omit geometry. A whole-mesh draw must not add
    // it.
    if (section.firstIndex != nextIndex) {
      return false;
    }
    nextIndex += section.indexCount;

    const auto *material = materials.find(section.material);
    if (material && material->alphaCutoff != 0.F) {
      return false;
    }
    const auto surface = resolveMeshSurface3D(mesh, material);
    if (surface.transparent() || !surface.state.writeDepth) {
      return false;
    }
    if (sharedState && !sameShadowState(*sharedState, surface.state)) {
      return false;
    }
    sharedState = surface.state;
  }
  return nextIndex == sections.indices->size();
}

bool hasCompleteSectionGroup(const std::vector<VisibleMesh3D> &meshes,
                             std::size_t first, std::size_t count) {
  if (count > meshes.size() - first) {
    return false;
  }
  for (std::size_t section = 0; section < count; ++section) {
    const auto &draw = meshes[first + section];
    if (draw.entity != meshes[first].entity ||
        draw.materialSection != section) {
      return false;
    }
  }
  return true;
}

} // namespace

void compactOpaqueShadowSections3D(std::vector<VisibleMesh3D> &meshes,
                                   const MaterialLibrary &materials) {
  std::size_t read = 0;
  std::size_t write = 0;
  while (read < meshes.size()) {
    auto draw = meshes[read];
    std::size_t consumed = 1;
    if (draw.materialSection == 0) {
      const auto *sections = visibleMeshSections3D(draw);
      if (sections &&
          hasCompleteSectionGroup(meshes, read, sections->sections.size()) &&
          canDrawWholeShadow(*draw.entity, *sections, materials)) {
        draw.drawAllSections = true;
        consumed = sections->sections.size();
      }
    }
    meshes[write] = std::move(draw);
    ++write;
    read += consumed;
  }
  meshes.resize(write);
}
} // namespace demi::runtime::render
