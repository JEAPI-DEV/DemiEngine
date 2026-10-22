#include "demi/runtime/render/BgfxRenderer3D.h"

#include "demi/runtime/render/bgfx3d/MeshTransform3D.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/model/Entity.h"
#include "demi/runtime/scene/model/MeshMaterialSections3D.h"

namespace demi::runtime::render {

bool BgfxRenderer3D::opaqueInstanceSourceEligible(
    const MeshInstancePreparation3D::EntitySource &source) const {
  if (!source.instances || !source.mesh || source.terrain || source.animation ||
      source.fade || source.relief || source.dentable ||
      !source.dents.empty()) {
    return false;
  }
  const auto &mesh = *source.mesh;
  if (!mesh.vertices.empty() || mesh.wireframe) {
    return false;
  }
  const auto supports = [&](const MaterialBinding *material) {
    if (material && material->program) {
      return false;
    }
    const auto surface = resolveMeshSurface3D(mesh, material);
    return !surface.transparent() && surface.state.writeDepth &&
           surface.state.topology == PrimitiveTopology::Triangles;
  };
  if (!mesh.material.empty()) {
    const auto *material = materials_.find(mesh.material);
    return material != nullptr && supports(material);
  }
  // Keep a source entirely on the ordinary path if any possible LOD requires
  // different semantics. No copy is partially owned by the compiled plan.
  for (const auto *model :
       {&mesh.model, &mesh.mediumLodModel, &mesh.lowLodModel}) {
    if (model->empty()) {
      continue;
    }
    const auto found = modelMeshes_.find(*model);
    if (found == modelMeshes_.end()) {
      return false;
    }
    const auto &sections = found->second->materialSections;
    if (sections) {
      for (const auto &section : sections->sections) {
        const auto *material = materials_.find(section.material);
        if (!material || !supports(material)) {
          return false;
        }
      }
    }
  }
  return supports(nullptr);
}

bool BgfxRenderer3D::compileOpaqueInstanceSource(
    const MeshInstancePreparation3D::EntitySource &source,
    const SceneVisibility3D &copies, const BgfxCameraFrame3D &frame,
    OpaqueInstancePlan3D &plan, std::string &error) {
  const auto &mesh = *source.mesh;
  // Tokens belong to the existing range-aware collector. Each selected LOD's
  // surface inputs are resolved once, then every copy appends only its matrix.
  std::unordered_map<const std::string *, std::vector<std::size_t>> lodGroups;
  for (const auto &copy : copies.meshes) {
    const auto lod =
        selectModelLod(mesh, nullptr, copy.transform.position,
                       frame.lodPosition.value_or(frame.position), false);
    if (lod.isCulled) {
      ++plan.statistics().distanceCulled;
      continue;
    }
    ++plan.statistics().visibleCopies;
    plan.statistics().mediumLodMeshes += lod.level == 1 ? 1U : 0U;
    plan.statistics().lowLodMeshes += lod.level == 2 ? 1U : 0U;
    std::array<float, 16> localMatrix;
    const auto *matrix = copy.preparedModelMatrix;
    if (matrix == nullptr) {
      localMatrix = composeMeshTransform3D(copy.transform, mesh.size);
      matrix = &localMatrix;
    }
    const auto existing = lodGroups.find(lod.model);
    if (existing != lodGroups.end()) {
      for (const auto token : existing->second) {
        plan.groups().appendTransform(token, *matrix);
      }
      continue;
    }
    const GpuMesh3D *geometry = nullptr;
    const MeshMaterialSections3D *sections = nullptr;
    if (lod.model->empty()) {
      geometry = primitiveMesh(mesh.shape, error);
    } else {
      const auto found = modelMeshes_.find(*lod.model);
      if (found == modelMeshes_.end()) {
        error = "Missing model geometry: " + *lod.model;
        return false;
      }
      geometry = &found->second->gpu;
      if (mesh.material.empty()) {
        sections = found->second->materialSections.get();
      }
    }
    if (!geometry) {
      return false;
    }
    std::vector<std::size_t> tokens;
    const auto add = [&](const std::string &materialId, std::uint32_t first,
                         std::uint32_t count) {
      const auto *material = materials_.find(materialId);
      const auto lighting = modelUnlit_.find(*lod.model);
      const bool unlit = (lighting != modelUnlit_.end() && lighting->second) ||
                         (material && material->unlit);
      MeshDrawSurface3D surface(mesh, material, {}, unlit);
      const auto &textureId = mesh.texture.empty() && material
                                  ? material->albedoTexture
                                  : mesh.texture;
      const auto texture = materialTexture(textureId, ImageMipSemantic::Color);
      if (!textureId.empty() && !texture.handle) {
        error = "Missing material colour texture: " + textureId;
        return false;
      }
      surface.metalRough[3] =
          texture.handle && !texture.srgbDecoded ? 1.F : 0.F;
      const auto token = plan.groups().addSurface(
          *geometry, texture.handle ? texture.handle : whiteTexture_, mesh,
          material, surface, usesMeshPbrSurface3D(material, mesh), *matrix,
          first, count);
      tokens.push_back(token);
      return true;
    };
    if (sections && !sections->sections.empty()) {
      for (const auto &section : sections->sections) {
        if (!add(section.material, section.firstIndex, section.indexCount)) {
          return false;
        }
      }
    } else if (!add(mesh.material, 0, UINT32_MAX)) {
      return false;
    }
    lodGroups.emplace(lod.model, std::move(tokens));
  }
  return true;
}

} // namespace demi::runtime::render
