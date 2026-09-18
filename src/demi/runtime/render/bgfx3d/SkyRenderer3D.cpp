#include "demi/runtime/render/bgfx3d/SkyRenderer3D.h"
#include "demi/runtime/render/bgfx3d/PrimitiveMeshFactory3D.h"

namespace demi::runtime::render {

bool SkyRenderer3D::draw(RenderCommands &commands, std::uint16_t view,
                         TextureHandle texture, Vec3 cameraPosition,
                         std::string &error) {
  if (!program_) {
    program_ = resources_.createBuiltinProgram(BuiltinProgram::Sky3D, error);
    sampler_ = resources_.createSampler("s_texColor", error);
    PrimitiveMeshData3D cube;
    if (!program_ || !sampler_ || !createPrimitiveMesh3D("cube", cube) ||
        !mesh_.upload(cube.positions, cube.textureCoordinates, cube.indices,
                      0xffffffffU, error)) {
      clear();
      return false;
    }
  }
  const std::array<float, 16> transform{1,
                                        0,
                                        0,
                                        0,
                                        0,
                                        1,
                                        0,
                                        0,
                                        0,
                                        0,
                                        1,
                                        0,
                                        cameraPosition.x,
                                        cameraPosition.y,
                                        cameraPosition.z,
                                        1};
  // Vertex shader places this at the far plane. It cannot obscure geometry,
  // irrespective of draw sorting, and never writes depth for later objects.
  return mesh_.draw(commands, view, program_, texture, sampler_, transform,
                    {.blend = BlendMode::Opaque,
                     .depthTest = DepthTest::LessEqual,
                     .cull = CullMode::None,
                     .writeDepth = false},
                    error);
}

void SkyRenderer3D::clear() {
  mesh_.clear();
  if (program_)
    resources_.destroy(program_);
  if (sampler_)
    resources_.destroy(sampler_);
  program_ = {};
  sampler_ = {};
}

} // namespace demi::runtime::render
