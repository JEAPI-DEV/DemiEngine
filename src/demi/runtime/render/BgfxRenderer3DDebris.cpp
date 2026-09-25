#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/render/bgfx3d/MeshTransform3D.h"
#include "demi/runtime/render/bgfx3d/PrimitiveMeshFactory3D.h"
#include <algorithm>

namespace demi::runtime::render {
bool BgfxRenderer3D::drawCosmeticDebris(
    std::vector<CosmeticFragment3D> &fragments, const BgfxCameraFrame3D &frame,
    std::span<const DrawUniformValue> lighting, ProgramHandle program,
    std::uint32_t &draws, std::uint32_t &triangles, std::string &error) {
  ProfileScope scope("Renderer3D.cosmetic_debris");
  const auto depth = [&](const CosmeticFragment3D &f) {
    return (f.position.x - frame.position.x) * frame.forward.x +
           (f.position.y - frame.position.y) * frame.forward.y +
           (f.position.z - frame.position.z) * frame.forward.z;
  };
  std::stable_sort(
      fragments.begin(), fragments.end(),
      [&](const auto &a, const auto &b) { return depth(a) > depth(b); });
  auto &cube = primitiveMeshes_["cube"];
  if (!cube)
    cube = std::make_unique<CachedMesh>(resources_);
  if (!cube->gpu.valid()) {
    PrimitiveMeshData3D mesh;
    if (!createPrimitiveMesh3D("cube", mesh) ||
        !cube->gpu.upload(mesh.positions, mesh.textureCoordinates, mesh.indices,
                          0xffffffffU, error))
      return false;
  }
  std::vector<DrawUniformValue> uniforms(lighting.begin(), lighting.end());
  const DrawState state{.blend = BlendMode::Alpha,
                        .depthTest = DepthTest::Less,
                        .cull = CullMode::None,
                        .topology = PrimitiveTopology::Triangles,
                        .writeDepth = false};
  for (const auto &fragment : fragments) {
    if (!frame.camera.renderMask.empty() && !fragment.renderLayer.empty() &&
        frame.camera.renderMask != fragment.renderLayer)
      continue;
    if (fragment.opacity() <= 0 || depth(fragment) < frame.camera.nearClip ||
        depth(fragment) > frame.camera.farClip)
      continue;
    const GpuMesh3D *mesh = &cube->gpu;
    if (!fragment.model.empty()) {
      const auto found = modelMeshes_.find(fragment.model);
      if (found == modelMeshes_.end()) {
        error = "Cosmetic debris model is not loaded: " + fragment.model;
        return false;
      }
      mesh = &found->second->gpu;
    }
    const std::array<float, 4> tint{fragment.color.r, fragment.color.g,
                                    fragment.color.b, fragment.opacity()};
    uniforms.front().values = tint;
    const auto texture = textures_.find(fragment.texture);
    if (!mesh->draw(commands_, frame.viewId, program,
                    texture.handle ? texture.handle : whiteTexture_,
                    meshSampler_,
                    composeMeshTransform3D(
                        {fragment.position, fragment.rotation, {1, 1, 1}},
                        fragment.size),
                    state, error, uniforms))
      return false;
    ++draws;
    triangles += mesh->indexCount()/3U;
  }
  return true;
}
} // namespace demi::runtime::render
