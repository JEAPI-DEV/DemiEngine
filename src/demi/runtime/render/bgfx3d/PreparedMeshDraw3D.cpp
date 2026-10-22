#include "demi/runtime/render/bgfx3d/PreparedMeshDraw3D.h"

namespace demi::runtime::render {

MeshDrawSurface3D::MeshDrawSurface3D(const MeshRendererComponent &mesh,
                                     const MaterialBinding *material,
                                     std::optional<float> fade, bool isUnlit)
    : surface(resolveMeshSurface3D(mesh, material, fade)),
      surfaceTint{mesh.color.r, mesh.color.g, mesh.color.b,
                  mesh.color.a * fade.value_or(1.0F) * surface.opacity},
      builtinSurfaceTint{surfaceTint[0] * surface.baseColor[0],
                         surfaceTint[1] * surface.baseColor[1],
                         surfaceTint[2] * surface.baseColor[2],
                         surfaceTint[3] * surface.baseColor[3]},
      metalRough(surface.shaderParameters()),
      alphaCutoff{material ? material->alphaCutoff : 0.0F, 0, 0, 0},
      unlit(isUnlit) {
  if (unlit) {
    metalRough[2] = 1.0F;
  }
}

PreparedMeshDraw3D::PreparedMeshDraw3D(const MeshRendererComponent &mesh,
                                       const MaterialBinding *material,
                                       std::optional<float> fade, bool unlit,
                                       bool depthOnly, bool overdraw,
                                       ProgramHandle defaultProgram)
    : PreparedMeshDraw3D(MeshDrawSurface3D(mesh, material, fade, unlit),
                         material, depthOnly, overdraw, defaultProgram) {}

PreparedMeshDraw3D::PreparedMeshDraw3D(const MeshDrawSurface3D &inputs,
                                       const MaterialBinding *material,
                                       bool depthOnly, bool overdraw,
                                       ProgramHandle defaultProgram)
    : MeshDrawSurface3D(inputs), state(surface.state),
      program(!depthOnly && material && material->program ? material->program
                                                          : defaultProgram) {
  if (overdraw) {
    state.blend = BlendMode::Additive;
    state.depthTest = DepthTest::Always;
    state.writeDepth = false;
  }
}

void MeshDrawSurface3D::fillUniforms(
    const std::array<DrawUniformValue, 8> &lighting,
    const MaterialBinding *material,
    std::vector<DrawUniformValue> &scratch) const {
  scratch.assign(lighting.begin(), lighting.end());
  // Custom programs own u_base_color; applying it in tint would double it.
  scratch[0].values =
      material && material->program ? surfaceTint : builtinSurfaceTint;
  scratch[1].values = alphaCutoff;
  scratch[7].values = metalRough;
  if (unlit) {
    scratch[3].values = noLight_;
    scratch[4].values = noLight_;
    scratch[5].values = unlitAmbient_;
  }
  if (material) {
    scratch.insert(scratch.end(), material->uniforms.begin(),
                   material->uniforms.end());
  }
}

} // namespace demi::runtime::render
