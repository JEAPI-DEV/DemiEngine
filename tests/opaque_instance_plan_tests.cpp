#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/bgfx3d/PreparedInstanceView3D.h"
#include "demi/runtime/scene/model/World.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <vector>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

class RecordedDraws final : public RenderCommands {
public:
  struct Draw {
    ProgramHandle program;
    TextureHandle texture;
    SamplerHandle sampler;
    BufferSlice indices;
    DrawState state;
    std::vector<std::byte> matrices;
    std::array<std::array<float, 4>, 8> uniforms{};
  };
  bool configureView2D(const View2DConfig &, std::string &) override {
    return true;
  }
  bool configureView3D(const View3DConfig &, std::string &) override {
    return true;
  }
  bool submit(const TransientDraw &, std::string &) override { return true; }
  bool submit(const BufferedDraw &, std::string &) override { return true; }
  bool submit(const InstancedBufferedDraw &input, std::string &) override {
    Draw draw{
        .program = input.program,
        .texture = input.texture,
        .sampler = input.sampler,
        .indices = input.indices,
        .state = input.state,
        .matrices = {input.instances.data.begin(), input.instances.data.end()}};
    assert(input.instances.stride == 64 && draw.matrices.size() == 128);
    assert(input.uniforms.size() >= draw.uniforms.size());
    for (std::size_t index = 0; index < draw.uniforms.size(); ++index) {
      assert(input.uniforms[index].values.size() >= 4);
      std::copy_n(input.uniforms[index].values.begin(), 4,
                  draw.uniforms[index].begin());
    }
    draws.push_back(std::move(draw));
    return true;
  }
  std::vector<Draw> draws;
};

void groupsRemainNeutralAcrossPasses(GpuResources &resources) {
  RecordedDraws recorded;
  std::size_t resolvedMaps = 0;
  MeshPbrBindings3D bindings(
      resources, recorded, [&](std::string_view, ImageMipSemantic semantic) {
        assert(semantic == ImageMipSemantic::Normal);
        ++resolvedMaps;
        return TextureView2D{TextureHandle{.index = 77, .generation = 1}, 1, 1,
                             false};
      });
  std::string error;
  GpuMesh3D mesh(resources);
  const std::array<Vec3, 4> positions{
      {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}}};
  const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
  assert(mesh.upload(positions, {}, indices, 0xffffffffU, error));
  MeshRendererComponent source;
  MaterialBinding first;
  first.state.depthTest = DepthTest::Less;
  first.state.writeDepth = true;
  first.state.cull = CullMode::Clockwise;
  first.alphaCutoff = 0.4F;
  first.usesPbrSurface = true;
  first.surface.maps[demi::assets::MaterialMapSlot::Normal] = "normal";
  MaterialBinding second = first;
  second.alphaCutoff = 0.7F;
  MeshDrawSurface3D firstSurface(source, &first, {}, false);
  MeshDrawSurface3D secondSurface(source, &second, {}, false);
  const std::array<float, 16> identity{1, 0, 0, 0, 0, 1, 0, 0,
                                       0, 0, 1, 0, 0, 0, 0, 1};
  auto translated = identity;
  translated[12] = 2;
  PreparedInstanceView3D pair;
  World world;
  BgfxCameraFrame3D frame;
  frame.cameraId = "first-camera";
  pair.beginView(world, frame, {});
  auto &plan = pair.opaquePlan();
  Entity owner;
  plan.classifySource(owner, true);
  const TextureHandle mask{.index = 78, .generation = 1};
  const auto firstGroup = plan.groups().addSurface(
      mesh, mask, source, &first, firstSurface, true, identity, 0, 3);
  const auto secondGroup = plan.groups().addSurface(
      mesh, mask, source, &second, secondSurface, true, identity, 3, 3);
  assert(firstGroup != secondGroup);
  plan.groups().appendTransform(firstGroup, translated);
  plan.groups().appendTransform(secondGroup, translated);
  plan.statistics().visibleCopies = 2;
  plan.statistics().mediumLodMeshes = 1;
  plan.finish();
  std::array<std::array<float, 4>, 8> values{};
  std::array<DrawUniformValue, 8> lighting;
  for (std::size_t index = 0; index < lighting.size(); ++index) {
    lighting[index].values = values[index];
  }
  const auto submit = [&](bool depth) {
    values[2][0] = depth ? 8 : 0;
    values[6][3] = depth ? 0.01F : 0;
    std::uint32_t draws = 0;
    std::uint32_t triangles = 0;
    const MeshSurfacePassPrograms3D pass{
        .pbrMesh = {.index = static_cast<std::uint16_t>(depth ? 11 : 21),
                    .generation = 1},
        .pbrInstanced = {.index = static_cast<std::uint16_t>(depth ? 12 : 22),
                         .generation = 1},
        .depthOnly = depth};
    assert(plan.groups().flush(recorded, depth ? 1 : 2, {}, {},
                               {.index = 79, .generation = 1}, {}, lighting,
                               draws, triangles, error, &bindings, {}, pass));
    assert(draws == 2 && triangles == 4);
  };
  submit(true);
  pair.beginView(world, frame, {});
  assert(plan.ready() && plan.groups().groupCount() == 2);
  submit(false);
  assert(recorded.draws.size() == 4 && resolvedMaps == 4);
  for (std::size_t index = 0; index < 2; ++index) {
    const auto &depth = recorded.draws[index];
    const auto &color = recorded.draws[index + 2];
    assert(depth.program.index == 12 && color.program.index == 22);
    assert(depth.indices.first == index * 3 && depth.indices.count == 3);
    assert(depth.indices.first == color.indices.first &&
           depth.indices.count == color.indices.count);
    assert(depth.matrices == color.matrices);
    assert(depth.texture == mask && color.texture == mask);
    assert(depth.sampler == color.sampler);
    assert(depth.uniforms[0] == color.uniforms[0]);
    assert(depth.uniforms[1][0] == (index == 0 ? 0.4F : 0.7F));
    assert(depth.uniforms[1] == color.uniforms[1]);
    assert(depth.uniforms[2][0] == 8 && color.uniforms[2][0] == 0);
    assert(depth.uniforms[6][3] == 0.01F && color.uniforms[6][3] == 0);
    assert(depth.state.cull == color.state.cull && depth.state.writeDepth &&
           color.state.writeDepth);
  }
  assert(plan.statistics().visibleCopies == 2 &&
         plan.statistics().mediumLodMeshes == 1);
  frame.cameraId = "second-camera";
  pair.beginView(world, frame, {});
  assert(!plan.ready() && plan.groups().groupCount() == 0);
  assert(!plan.sourceEligibility(owner).has_value());
  assert(plan.statistics().visibleCopies == 0);
  bool rejectedOldToken = false;
  try {
    plan.groups().appendTransform(firstGroup, identity);
  } catch (const std::out_of_range &) {
    rejectedOldToken = true;
  }
  assert(rejectedOldToken);
  pair.reset();
  assert(!plan.ready() && plan.groups().groupCount() == 0);
}

void identicalSurfaceOverridesShareBatches(GpuResources &resources) {
  std::string error;
  GpuMesh3D mesh(resources);
  const std::array<Vec3, 3> vertices{{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}}};
  const std::array<std::uint32_t, 3> indices{0, 1, 2};
  assert(mesh.upload(vertices, {}, indices, 0xffffffffU, error));
  MeshRendererComponent a;
  a.materialNumbers = {{"foliage_transmission", .7F}, {"ambient_occlusion_strength", .4F}};
  MeshRendererComponent b;
  b.materialNumbers = {{"ambient_occlusion_strength", .4F}, {"foliage_transmission", .7F}};
  MaterialBinding material;
  MeshDrawSurface3D first(a, &material, {}, false);
  MeshDrawSurface3D second(b, &material, {}, false);
  const std::array<float, 16> identity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
  MeshInstanceBatch3D batch;
  const auto one = batch.addSurface(mesh, {}, a, &material, first, true, identity, 0, 3);
  const auto two = batch.addSurface(mesh, {}, b, &material, second, true, identity, 0, 3);
  assert(one == two && batch.groupCount() == 1);
  b.materialNumbers["foliage_transmission"] = .2F;
  const auto different = batch.addSurface(mesh, {}, b, &material, second, true, identity, 0, 3);
  assert(different != one && batch.groupCount() == 2);
}

} // namespace

int main() {
  std::string error;
  BgfxGraphicsDevice device;
  assert(device.initialize(
      {.api = GraphicsApi::Noop, .width = 32, .height = 32}, error));
  auto resources = createBgfxGpuResources();
  groupsRemainNeutralAcrossPasses(*resources);
  identicalSurfaceOverridesShareBatches(*resources);
}
