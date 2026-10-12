#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/render/bgfx3d/PreparedMeshDraw3D.h"
#include "demi/runtime/render/bgfx3d/MeshInstanceBatch3D.h"
#include "demi/runtime/terrain/TerrainRenderLod.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include <cmath>
#include <numbers>
#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"
#include "demi/runtime/geometry/MeshDeformation3D.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/destruction/DestructionWorld3D.h"
#include "demi/runtime/destruction/DetachedFragmentFade3D.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/model/MeshMaterialSections3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/BoxCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Environment3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/DirectionalLightComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Dentable3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ParticleEmitter3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"

#include <algorithm>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

class MeshUploadCapture final : public GpuResources {
public:
  explicit MeshUploadCapture(GpuResources &target) : target_(target) {}
  std::vector<std::vector<GpuMeshVertex3D>> meshUploads;
  bool failNextMeshUpload = false;
  RenderDeviceLimits limits() const override { return target_.limits(); }
  TextureHandle createTexture(const TextureCreateInfo &info,
                              std::string &error) override {
    return target_.createTexture(info, error);
  }
  bool updateTexture(TextureHandle handle, const TextureUpdateInfo &info,
                     std::string &error) override {
    return target_.updateTexture(handle, info, error);
  }
  SamplerHandle createSampler(std::string_view name,
                              std::string &error) override {
    return target_.createSampler(name, error);
  }
  UniformHandle createUniform(std::string_view name, UniformType type,
                              std::uint16_t count,
                              std::string &error) override {
    return target_.createUniform(name, type, count, error);
  }
  BufferHandle createBuffer(const BufferCreateInfo &info,
                            std::string &error) override {
    if (failNextMeshUpload && info.kind == BufferKind::Vertex &&
        info.debugName == "3D mesh vertices") {
      failNextMeshUpload = false;
      error = "Injected terrain upload failure";
      return {};
    }
    const auto handle = target_.createBuffer(info, error);
    if (handle && info.kind == BufferKind::Vertex &&
        info.debugName == "3D mesh vertices") {
      assert(info.data.size() % sizeof(GpuMeshVertex3D) == 0);
      auto &vertices =
          meshUploads.emplace_back(info.data.size() / sizeof(GpuMeshVertex3D));
      std::memcpy(vertices.data(), info.data.data(), info.data.size());
    }
    return handle;
  }
  bool updateBuffer(BufferHandle handle, std::span<const std::byte> data,
                    std::string &error) override {
    return target_.updateBuffer(handle, data, error);
  }
  ProgramHandle createProgram(const ProgramCreateInfo &info,
                              std::string &error) override {
    return target_.createProgram(info, error);
  }
  ProgramHandle createBuiltinProgram(BuiltinProgram program,
                                     std::string &error) override {
    return target_.createBuiltinProgram(program, error);
  }
  std::string_view shaderBackend() const override {
    return target_.shaderBackend();
  }
  RenderTargetHandles createRenderTarget(const RenderTargetCreateInfo &info,
                                         std::string &error) override {
    return target_.createRenderTarget(info, error);
  }
  bool destroy(TextureHandle handle) override {
    return target_.destroy(handle);
  }
  bool destroy(SamplerHandle handle) override {
    return target_.destroy(handle);
  }
  bool destroy(UniformHandle handle) override {
    return target_.destroy(handle);
  }
  bool destroy(BufferHandle handle) override { return target_.destroy(handle); }
  bool destroy(ProgramHandle handle) override {
    return target_.destroy(handle);
  }
  bool destroy(FrameBufferHandle handle) override {
    return target_.destroy(handle);
  }
  void clear() override { target_.clear(); }

private:
  GpuResources &target_;
};

class TextureCapture final : public RenderCommands {
public:
  explicit TextureCapture(RenderCommands &target) : target_(target) {}
  std::vector<TextureHandle> textures;
  std::vector<DrawState> bufferedStates;
  std::vector<BufferSlice> bufferedIndices;
  std::size_t instanceDrawCalls = 0;
  bool forwardDraws = true;
  std::vector<float> bufferedAlpha;
  std::vector<std::array<float, 4>> bufferedTint;
  std::vector<std::array<float, 4>> bufferedMetalRough;
  std::vector<std::array<float, 16>> bufferedTransforms;
  std::vector<View2DConfig> views2D;
  std::vector<View3DConfig> views3D;
  std::map<std::uint16_t,std::vector<std::array<float,16>>> transformsByView;
  std::map<std::uint16_t,TextureHandle> shadowTextures;
  bool configureView2D(const View2DConfig &view, std::string &error) override {
    views2D.push_back(view);
    return target_.configureView2D(view, error);
  }
  bool configureView3D(const View3DConfig &view, std::string &error) override {
    views3D.push_back(view);
    return target_.configureView3D(view, error);
  }
  bool submit(const TransientDraw &draw, std::string &error) override {
    return target_.submit(draw, error);
  }
  bool submit(const BufferedDraw &draw, std::string &error) override {
    transformsByView[draw.viewId].push_back(draw.transform);
    for(const auto &texture:draw.textures) if(texture.stage==1)shadowTextures[draw.viewId]=texture.texture;
    textures.push_back(draw.texture);
    bufferedStates.push_back(draw.state);
    bufferedIndices.push_back(draw.indices);
    bufferedAlpha.push_back(draw.uniforms.empty()?1.F:draw.uniforms.front().values[3]);
    if (!draw.uniforms.empty())
      bufferedTint.push_back({draw.uniforms.front().values[0],
                              draw.uniforms.front().values[1],
                              draw.uniforms.front().values[2],
                              draw.uniforms.front().values[3]});
    if (draw.uniforms.size() > 7)
      bufferedMetalRough.push_back({draw.uniforms[7].values[0],
                                  draw.uniforms[7].values[1],
                                  draw.uniforms[7].values[2],
                                  draw.uniforms[7].values[3]});
    bufferedTransforms.push_back(draw.transform);
    return forwardDraws ? target_.submit(draw, error) : true;
  }
  bool submit(const InstancedBufferedDraw &draw, std::string &error) override {
    ++instanceDrawCalls;
    for(const auto &transform:draw.transforms)transformsByView[draw.viewId].push_back(transform);
    for(const auto &texture:draw.textures)if(texture.stage==1)shadowTextures[draw.viewId]=texture.texture;
    textures.push_back(draw.texture);
    return forwardDraws ? target_.submit(draw, error) : true;
  }
private:
  RenderCommands &target_;
};

Entity shape(std::string id, std::string shapeName, const Vec3 position) {
  Entity entity;
  entity.id = std::move(id);
  entity.setComponent(Transform3DComponent{.position = position});
  entity.setComponent(MeshRendererComponent{.shape = std::move(shapeName),
                                            .size = {1.0F, 1.0F, 1.0F},
                                            .color = {0.3F, 0.7F, 0.9F, 1.0F}});
  return entity;
}

} // namespace

int main() {
  {
    MeshRendererComponent mesh;
    mesh.color = {0.5F, 0.5F, 0.5F, 0.5F};
    MaterialBinding material;
    material.baseColor = {0.5F, 0.25F, 1, 0.5F};
    material.opacity = 0.5F;
    material.program = {.index = 1, .generation = 1};
    std::array<DrawUniformValue, 8> lighting;
    std::vector<DrawUniformValue> scratch;
    PreparedMeshDraw3D custom(mesh, &material, std::nullopt, true, false,
                              false, {});
    auto moved = std::move(custom);
    moved.fillUniforms(lighting, &material, scratch);
    assert(scratch[0].values.data() == moved.surfaceTint.data());
    assert(scratch[0].values[0] == 0.5F);
    assert(scratch[0].values[3] == 0.25F);
    assert(scratch[7].values[2] == 1.0F);
    assert(scratch[3].values[0] == 0.0F);
    assert(scratch[5].values[0] == 1.0F);

    material.program = {};
    PreparedMeshDraw3D builtin(mesh, &material, 0.5F, false, false, false, {});
    builtin.fillUniforms(lighting, &material, scratch);
    assert(scratch[0].values.data() == builtin.builtinSurfaceTint.data());
    assert(scratch[0].values[0] == 0.25F);
    assert(scratch[0].values[3] == 0.0625F);
    assert(scratch[7].values[2] == 0.0F);
    assert(builtin.state.blend == BlendMode::Alpha);
    assert(!builtin.state.writeDepth);
  }
  {
    auto chunk = std::make_shared<PreparedTerrainChunkLod>();
    chunk->boundsMin = {-1, -1, -1};
    chunk->boundsMax = {1, 1, 1};
    chunk->geometricErrors = {0, 0.1F, 0.2F, 0.4F};
    chunk->triangleCounts = {4, 3, 2, 1};
    BgfxCameraFrame3D camera;
    camera.cameraId = "hysteresis-camera";
    camera.camera.perspective = false;
    camera.camera.orthographicSize = 100;
    camera.viewportHeight = 200;
    WorldTransform3D transform;
    assert(selectTerrainDrawLevel3D(*chunk, transform, {1, 1, 1}, camera, 0.5F) == 3);
    transform.scale = {10, 1, 1};
    assert(selectTerrainDrawLevel3D(*chunk, transform, {1, 1, 1}, camera, 0.5F) == 3);
    transform.scale = {1, 10, 1};
    assert(selectTerrainDrawLevel3D(*chunk, transform, {1, 1, 1}, camera, 2.0F) == 2);
    transform = {};
    TerrainDrawSelection3D selection;
    selection.beginPresentation();
    assert(selection.select(chunk, transform, {1, 1, 1}, camera, 0.38F) == 2);
    assert(selection.select(chunk, transform, {1, 1, 1}, camera, 0.42F) == 2);
    assert(selection.select(chunk, transform, {1, 1, 1}, camera, 0.5F) == 3);
    assert(selection.select(chunk, transform, {1, 1, 1}, camera, 0.39F) == 2);
    camera.cameraId = "independent-camera";
    assert(selection.select(chunk, transform, {1, 1, 1}, camera, 0.42F) == 3);
    // Two placements may share immutable prepared geometry, but not quality
    // transition history. One owner's demotion band cannot hold back another.
    camera.cameraId = "shared-placement-camera";
    assert(selection.select(chunk, transform, {1, 1, 1}, camera, 0.38F, "owner-a") == 2);
    assert(selection.select(chunk, transform, {1, 1, 1}, camera, 0.42F, "owner-a") == 2);
    assert(selection.select(chunk, transform, {1, 1, 1}, camera, 0.42F, "owner-b") == 3);
    chunk->triangleCounts = {4, 4, 4, 4};
    assert(selectTerrainDrawLevel3D(*chunk, transform, {1, 1, 1}, camera, 100) == 0);
  }
  {
    PreparedTerrainChunkLod chunk;
    chunk.boundsMin = {-1000, -1, -1};
    chunk.boundsMax = {1000, 1, 1};
    chunk.geometricErrors = {0, 0.03F, 0.06F, 0.1F};
    chunk.triangleCounts = {100, 60, 30, 15};
    BgfxCameraFrame3D camera;
    camera.position = {0, 0, 100};
    camera.forward = {0, 0, -1};
    camera.camera.fov = 60;
    camera.viewportHeight = 1000;
    // A wide chunk's bounding sphere crosses the camera plane even though its
    // actual terrain bounds are entirely in front. Local Y error does not
    // gain arbitrary horizontal/depth displacement merely from this width.
    assert(selectTerrainDrawLevel3D(chunk, {}, {1, 1, 1}, camera, 1.0F) == 3);
    chunk.boundsMin.z = 99.95F;
    chunk.boundsMax.z = 101;
    assert(selectTerrainDrawLevel3D(chunk, {}, {1, 1, 1}, camera, 1.0F) == 0);

    chunk.boundsMin = {-2, -10, -2};
    chunk.boundsMax = {2, 10, 2};
    chunk.geometricErrors = {0, 0.05F, 0.1F, 0.25F};
    camera.position = {0, 3, 100};
    const auto project = [&camera](Vec3 point) {
      const float x = point.x - camera.position.x;
      const float y = point.y - camera.position.y;
      if (!camera.camera.perspective) {
        const float factor = float(camera.viewportHeight) /
                             (2 * camera.camera.orthographicSize);
        return Vec2{x * factor, y * factor};
      }
      const float depth = camera.position.z - point.z;
      const float factor = float(camera.viewportHeight) /
                           (2 * std::tan(camera.camera.fov *
                                         std::numbers::pi_v<float> / 360));
      return Vec2{x * factor / depth, y * factor / depth};
    };
    for (const bool perspective : {true, false}) {
      camera.camera.perspective = perspective;
      camera.camera.orthographicSize = 50;
      for (const Vec3 rotation : {Vec3{}, Vec3{35, 40, 25}, Vec3{80, 0, 10},
                                  Vec3{0, 0, 90}}) {
        WorldTransform3D transform;
        transform.scale = {7, 3, 2};
        transform.rotation = rotation;
        const auto level = selectTerrainDrawLevel3D(chunk, transform, {1, 1, 1},
                                                    camera, 12.0F);
        assert(level == 3);
        const float heightError = chunk.geometricErrors[level];
        for (const float x : {-2.0F, 0.0F, 2.0F}) {
          for (const float y : {-9.0F, 0.0F, 9.0F}) {
            for (const float z : {-2.0F, 0.0F, 2.0F}) {
              const auto full = project(transformPoint3D(transform, {x, y, z}));
              for (const float direction : {-1.0F, 1.0F}) {
                const auto reduced = project(transformPoint3D(
                    transform, {x, y + direction * heightError, z}));
                assert(std::hypot(full.x - reduced.x, full.y - reduced.y) <= 12.0F);
              }
            }
          }
        }
      }
    }
  }
  BgfxGraphicsDevice graphics;
  std::string error;
  assert(graphics.initialize(GraphicsDeviceConfig{.api = GraphicsApi::Noop,
                                                  .width = 320,
                                                  .height = 180,
                                                  .vsync = false},
                             error));
  auto resources = createBgfxGpuResources();
  auto commands = createBgfxRenderCommands(*resources);
  {
    // Capture command intent without backend validation so recycled texture
    // generations can be compared together, including the stale generation.
    TextureCapture batchCapture(*commands);
    batchCapture.forwardDraws = false;
    GpuMesh3D firstMesh(*resources);
    GpuMesh3D secondMesh(*resources);
    const std::array<Vec3, 3> positions{{{-1, 0, 0}, {1, 0, 0}, {0, 1, 0}}};
    assert(firstMesh.upload(positions, {}, {}, 0xffffffffU, error));
    assert(secondMesh.upload(positions, {}, {}, 0xffffffffU, error));
    const TextureHandle texture{.index = 7, .generation = 1};
    const TextureHandle recycled{.index = 7, .generation = 2};
    const std::array<float, 4> tint{0.5F, 1, 1, 1};
    auto distinctTint = tint;
    distinctTint[0] = std::nextafter(tint[0], 1.0F);
    const std::array<float, 4> metalRough{0, 0.8F, 0, 0};
    auto distinctMetalRough = metalRough;
    distinctMetalRough[1] = std::nextafter(metalRough[1], 1.0F);
    const std::array<float, 16> transform{
        1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    std::array<DrawUniformValue, 8> lighting;
    for (auto &uniform : lighting) {
      uniform.values = tint;
    }
    MeshInstanceBatch3D batches;
    batches.add(firstMesh, texture, tint, metalRough, false, transform);
    batches.add(secondMesh, texture, tint, metalRough, false, transform);
    batches.add(firstMesh, recycled, tint, metalRough, false, transform);
    batches.add(firstMesh, texture, distinctTint, metalRough, false, transform);
    batches.add(firstMesh, texture, tint, metalRough, true, transform);
    batches.add(firstMesh, texture, tint, distinctMetalRough, false, transform);
    batches.add(firstMesh, texture, tint, metalRough, false, transform);
    std::uint32_t draws = 0;
    std::uint32_t triangles = 0;
    assert(batches.flush(batchCapture, 0, {}, {}, {}, {}, lighting,
                         draws, triangles, error));
    assert(draws == 6);
    assert(triangles == 7);
    assert(batchCapture.instanceDrawCalls == 1);
    assert(batchCapture.textures.size() == 6);
    assert(batchCapture.textures[0] == texture);
    assert(batchCapture.textures[1] == texture);
    assert(batchCapture.textures[2] == recycled);
    assert(batchCapture.bufferedTint[2][0] == distinctTint[0]);
    assert(batchCapture.bufferedMetalRough.back()[1] == distinctMetalRough[1]);
  }
  TextureCapture capture(*commands);
  BgfxRenderer3D renderer(*resources, capture, false); // CPU fallback reference.
  assert(renderer.initialize(error));
  assert(renderer.initialize(error));
  std::vector<std::string> diagnostics;
  demi::AssetRegistry registry;
  registry.assets.push_back(
      {.id = "asset://textures/override", .type = "Texture2D",
       .sourcePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                     "examples/minimal_3d/assets/textures/checker.png"});
  registry.assets.push_back(
      {.id = "asset://models/animated",
       .type = "Model3D",
       .sourcePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                     "examples/animation_3d/assets/AnimationLib/"
                     "UAL1_Standard.glb"});
  for (const std::string id :
       {"asset://models/lod_high", "asset://models/lod_medium",
        "asset://models/lod_low"}) {
    registry.assets.push_back(
        {.id = id,
         .type = "Model3D",
         .sourcePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                       "examples/animation_3d/assets/AnimationLib/"
                       "UAL1_Standard.glb"});
  }
  registry.assets.push_back(
      {.id = "asset://targets/test",
       .type = "RenderTarget",
       .sourcePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                     "examples/minimal_voxel/assets/targets/"
                     "voxel_minimap.target.json"});
  assert(renderer.loadAssets(registry, diagnostics));

  World world;
  world.ui.canvasSize = {320.0F, 180.0F};
  world.ui.nodes.push_back({.id = "status",
                            .type = "label",
                            .text = "BGFX 3D",
                            .resolved = {4.0F, 4.0F, 120.0F, 24.0F},
                            .fontSize = 18.0F});
  Entity parent = shape("parent", "cube", {0.0F, 0.0F, 0.0F});
  world.entities.push_back(std::move(parent));
  Entity child = shape("child", "sphere", {1.0F, 0.0F, 0.0F});
  child.component<Transform3DComponent>()->parent = "parent";
  world.entities.push_back(std::move(child));
  world.entities.push_back(shape("plane", "plane", {0.0F, -1.0F, 0.0F}));
  world.entities.push_back(shape("cylinder", "cylinder", {-1.0F, 0.0F, 0.0F}));
  Entity procedural = shape("procedural", "", {0.0F, 1.0F, 0.0F});
  procedural.component<MeshRendererComponent>()->vertices = {
      {-0.5F, 0.0F, 0.0F}, {0.5F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
  procedural.component<MeshRendererComponent>()->uvs = {
      {0.0F, 0.0F}, {1.0F, 0.0F}, {0.5F, 1.0F}};
  world.entities.push_back(std::move(procedural));
  Entity lodModel;
  lodModel.id = "lod_model";
  lodModel.setComponent(Transform3DComponent{});
  lodModel.setComponent(MeshRendererComponent{
      .model = "asset://models/lod_high",
      .mediumLodModel = "asset://models/lod_medium",
      .mediumLodDistance = 4.0F,
      .lowLodModel = "asset://models/lod_low",
      .lowLodDistance = 10.0F,
  });
  world.entities.push_back(std::move(lodModel));

  const BgfxCameraFrame3D frame{
      .camera = {.clearColor = {0.05F, 0.06F, 0.09F, 1.0F}},
      .position = {0.0F, 2.0F, 5.0F},
      .forward = {0.0F, -0.3F, -1.0F},
      .viewportWidth = 320,
      .viewportHeight = 180,
  };
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  assert(renderer.statistics().mediumLodMeshes == 1U);
  assert(renderer.statistics().lowLodMeshes == 0U);
  assert(renderer.statistics().batches >= 1);
  assert(renderer.statistics().triangles > 12);
  const std::uint32_t batchesWithoutDebugGeometry =
      renderer.statistics().batches;
  static_cast<void>(graphics.endFrame());

  {
    MeshUploadCapture uploads(*resources);
    TextureCapture sectionDraws(*commands);
    BgfxRenderer3D sectionRenderer(uploads, sectionDraws);
    assert(sectionRenderer.initialize(error));

    World sectionWorld;
    auto panel = shape("sectioned_inline", "", {});
    auto *mesh = panel.component<MeshRendererComponent>();
    mesh->vertices = {{-1, 0, 0}, {0, 0, 0}, {-1, 1, 0},
                      {0, 0, 0}, {1, 0, 0}, {1, 1, 0}};
    mesh->normals.assign(6, {0, 0, 1});
    mesh->uvs.assign(6, {0, 0});
    mesh->markGeometryChanged();

    MeshMaterialSections3D sections;
    sections.indices = std::make_shared<const std::vector<std::uint32_t>>(
        std::vector<std::uint32_t>{0, 1, 2, 3, 4, 5});
    sections.sections = {{"left", {}, 0, 3}, {"right", {}, 3, 3}};
    sections.indexRevision = 1;
    panel.setComponent(sections);
    sectionWorld.entities.push_back(std::move(panel));

    assert(sectionRenderer.renderFrame(sectionWorld, frame, 0.016F, error));
    assert(sectionDraws.bufferedIndices.size() == 2);
    assert(sectionDraws.bufferedIndices[0].first == 0);
    assert(sectionDraws.bufferedIndices[1].first == 3);
    assert(sectionDraws.bufferedIndices[0].count == 3);
    assert(sectionDraws.bufferedIndices[1].count == 3);
    assert(uploads.meshUploads.size() == 1);
    static_cast<void>(graphics.endFrame());

    auto *published =
        sectionWorld.entities.front().component<MeshMaterialSections3D>();
    published->sections.front().material = "asset://changed_binding";
    assert(sectionRenderer.renderFrame(sectionWorld, frame, 0.016F, error));
    assert(uploads.meshUploads.size() == 1);
    static_cast<void>(graphics.endFrame());

    Entity sun;
    sun.id = "section_shadow_light";
    sun.setComponent(DirectionalLightComponent{.castsShadows = true});
    sectionWorld.entities.push_back(std::move(sun));
    assert(sectionRenderer.renderFrame(sectionWorld, frame, 0.016F, error));
    assert(sectionRenderer.statistics().shadowPasses > 0);
    assert(uploads.meshUploads.size() == 1);
    static_cast<void>(graphics.endFrame());
  }

  {
    MeshUploadCapture uploads(*resources);
    BgfxRenderer3D retentionRenderer(uploads, *commands);
    assert(retentionRenderer.initialize(error));
    World retentionWorld;
    auto triangle = shape("retained-inline", "", {});
    auto *mesh = triangle.component<MeshRendererComponent>();
    mesh->vertices = {{-1, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh->hasBounds = true;
    mesh->boundsMin = {-1, 0, 0};
    mesh->boundsMax = {1, 1, 0};
    mesh->markGeometryChanged();
    retentionWorld.entities.push_back(std::move(triangle));

    assert(retentionRenderer.renderFrame(retentionWorld, frame, 0.016F, error));
    assert(uploads.meshUploads.size() == 1);
    static_cast<void>(graphics.endFrame());

    // Camera visibility must not control GPU resource ownership.
    auto *transform = retentionWorld.entities.front().component<Transform3DComponent>();
    transform->position = {10000, 0, 0};
    assert(extractVisibleMeshes3D(retentionWorld, frame).meshes.empty());
    assert(retentionRenderer.renderFrame(retentionWorld, frame, 0.016F, error));
    static_cast<void>(graphics.endFrame());
    transform->position = {};
    assert(retentionRenderer.renderFrame(retentionWorld, frame, 0.016F, error));
    assert(uploads.meshUploads.size() == 1);
    static_cast<void>(graphics.endFrame());

    // A failed frame cannot evict absent resources: the authoring document
    // may be restored immediately after an unsuccessful preview rebuild.
    Entity retained = std::move(retentionWorld.entities.front());
    retentionWorld.entities.clear();
    retentionWorld.entities.push_back(shape("invalid-preview", "invalid", {}));
    assert(!retentionRenderer.renderFrame(retentionWorld, frame, 0.016F, error));
    retentionWorld.entities.clear();
    retentionWorld.entities.push_back(std::move(retained));
    error.clear();
    assert(retentionRenderer.renderFrame(retentionWorld, frame, 0.016F, error));
    assert(uploads.meshUploads.size() == 1);
    static_cast<void>(graphics.endFrame());

    // Successful entity removal releases ownership; recreating the same ID
    // must upload again instead of borrowing stale geometry.
    retained = std::move(retentionWorld.entities.front());
    retentionWorld.entities.clear();
    assert(retentionRenderer.renderFrame(retentionWorld, frame, 0.016F, error));
    static_cast<void>(graphics.endFrame());
    retentionWorld.entities.push_back(std::move(retained));
    assert(retentionRenderer.renderFrame(retentionWorld, frame, 0.016F, error));
    assert(uploads.meshUploads.size() == 2);
    static_cast<void>(graphics.endFrame());
  }
  {
    MeshUploadCapture uploads(*resources);
    TextureCapture coloredDraws(*commands);
    BgfxRenderer3D coloredRenderer(uploads, coloredDraws);
    assert(coloredRenderer.initialize(error));
    World coloredWorld;
    Entity panel = shape("colored_inline", "", {});
    auto *mesh = panel.component<MeshRendererComponent>();
    mesh->color = {1, 1, 1, 1};
    mesh->opacity = 1;
    mesh->surfaceMode = "transparent";
    mesh->vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}};
    mesh->vertexColors = {{1, 0, 0, 1}, {0, 1, 0, 0.5F}, {0, 0, 1, 0}};
    mesh->markGeometryChanged();
    coloredWorld.entities.push_back(std::move(panel));
    mesh = coloredWorld.entities.front().component<MeshRendererComponent>();
    uploads.meshUploads.clear();
    assert(coloredRenderer.renderFrame(coloredWorld, frame, 0.016F, error));
    assert(uploads.meshUploads.size() == 1);
    assert(uploads.meshUploads.back()[0].rgba == 0xff0000ffU);
    assert(uploads.meshUploads.back()[1].rgba == 0x8000ff00U);
    assert(uploads.meshUploads.back()[2].rgba == 0x00ff0000U);
    assert(
        (coloredDraws.bufferedTint.back() == std::array<float, 4>{1, 1, 1, 1}));
    assert(coloredDraws.bufferedStates.back().blend == BlendMode::Alpha);
    static_cast<void>(graphics.endFrame());
    assert(coloredRenderer.renderFrame(coloredWorld, frame, 0.016F, error));
    assert(uploads.meshUploads.size() == 1);
    static_cast<void>(graphics.endFrame());

    // Equal-sized color edits need a revision refresh, while tint changes
    // remain draw-time multipliers and do not replace vertex buffers.
    mesh->vertexColors[1] = {0.25F, 0.5F, 0.75F, 0.25F};
    mesh->markGeometryChanged();
    assert(coloredRenderer.renderFrame(coloredWorld, frame, 0.016F, error));
    assert(uploads.meshUploads.size() == 2);
    assert(uploads.meshUploads.back()[1].rgba == 0x40bf8040U);
    static_cast<void>(graphics.endFrame());
    mesh->color = {0.25F, 0.5F, 0.75F, 0.5F};
    mesh->opacity = 0.5F;
    assert(coloredRenderer.renderFrame(coloredWorld, frame, 0.016F, error));
    assert(uploads.meshUploads.size() == 2);
    assert(coloredDraws.bufferedTint.back()[0] == mesh->color.r);
    assert(coloredDraws.bufferedTint.back()[3] == 0.25F);
    static_cast<void>(graphics.endFrame());

    // Dent refinement keeps original colors and interpolates new vertices;
    // the source revision still invalidates an already dented GPU mesh.
    const std::array<std::uint32_t, 3> sourceColors{
        packVertexColorRgba8(mesh->vertexColors[0]),
        packVertexColorRgba8(mesh->vertexColors[1]),
        packVertexColorRgba8(mesh->vertexColors[2])};
    coloredWorld.entities.front().setComponent(Dentable3DComponent{});
    assert(dentMesh3D(coloredWorld, "colored_inline", {0.25F, 0.25F, 0},
                      {0, 0, -1}, 0.1F, 0.03F, error));
    assert(coloredRenderer.renderFrame(coloredWorld, frame, 0.016F, error));
    assert(uploads.meshUploads.size() == 3);
    assert(uploads.meshUploads.back().size() > mesh->vertices.size());
    for (std::size_t i = 0; i < sourceColors.size(); ++i)
      assert(uploads.meshUploads.back()[i].rgba == sourceColors[i]);
    static_cast<void>(graphics.endFrame());
    assert(coloredRenderer.renderFrame(coloredWorld, frame, 0.016F, error));
    assert(uploads.meshUploads.size() == 3);
    static_cast<void>(graphics.endFrame());
    mesh->vertexColors[0] = {0, 1, 1, 0.75F};
    mesh->markGeometryChanged();
    assert(coloredRenderer.renderFrame(coloredWorld, frame, 0.016F, error));
    assert(uploads.meshUploads.size() == 4);
    assert(uploads.meshUploads.back()[0].rgba == 0xbfffff00U);
    static_cast<void>(graphics.endFrame());
    assert(resetMeshDents3D(coloredWorld, "colored_inline"));
    assert(coloredRenderer.renderFrame(coloredWorld, frame, 0.016F, error));
    assert(uploads.meshUploads.size() == 5);
    assert(uploads.meshUploads.back().size() == mesh->vertices.size());
    assert(uploads.meshUploads.back()[0].rgba == 0xbfffff00U);
    static_cast<void>(graphics.endFrame());
    mesh->vertexColors.clear();
    mesh->markGeometryChanged();
    assert(coloredRenderer.renderFrame(coloredWorld, frame, 0.016F, error));
    assert(uploads.meshUploads.size() == 6);
    for (const auto &vertex : uploads.meshUploads.back())
      assert(vertex.rgba == 0xffffffffU);
    static_cast<void>(graphics.endFrame());

    // Native publishers get the same validation on a cache refresh.
    mesh->vertexColors = {{1, 1, 1, 1}};
    assert(!coloredRenderer.renderFrame(coloredWorld, frame, 0.016F, error));
    assert(error.find("vertex_colors") != std::string::npos);
    assert(uploads.meshUploads.size() == 6);
  }
  {
    World surfaces;
    auto near = shape("transparent-near", "cube", {0, 0, 1});
    near.component<MeshRendererComponent>()->surfaceMode = "transparent";
    near.component<MeshRendererComponent>()->opacity = 0.5F;
    near.component<MeshRendererComponent>()->roughness = 0.25F;
    near.component<MeshRendererComponent>()->materialColors["base_color"] =
        {0.5F, 0.25F, 1.0F, 0.5F};
    auto opaque = shape("opaque", "cube", {0, 0, 0});
    auto smoothOpaque = shape("smooth-opaque", "cube", {2, 0, 0});
    smoothOpaque.component<MeshRendererComponent>()->roughness = 0.0F;
    auto far = shape("transparent-far", "cube", {0, 0, -3});
    far.component<MeshRendererComponent>()->surfaceMode = "additive";
    far.component<MeshRendererComponent>()->metallic = 0.75F;
    surfaces.entities.push_back(std::move(near));
    surfaces.entities.push_back(std::move(opaque));
    surfaces.entities.push_back(std::move(smoothOpaque));
    surfaces.entities.push_back(std::move(far));
    auto surfaceFrame = frame;
    surfaceFrame.camera.renderHud = false;
    capture.bufferedStates.clear();
    capture.bufferedAlpha.clear();
    capture.bufferedTint.clear();
    capture.bufferedMetalRough.clear();
    capture.bufferedTransforms.clear();
    capture.views3D.clear();
    assert(renderer.renderFrame(surfaces, surfaceFrame, 0.016F, error));
    assert(capture.views3D.back().sequential);
    assert(capture.bufferedStates.size() == 4);
    assert(capture.bufferedStates[0].blend == BlendMode::Opaque);
    assert(capture.bufferedStates[1].blend == BlendMode::Opaque);
    assert((capture.bufferedMetalRough[0][1] == 0.0F &&
            capture.bufferedMetalRough[1][1] == 0.8F) ||
           (capture.bufferedMetalRough[0][1] == 0.8F &&
            capture.bufferedMetalRough[1][1] == 0.0F));
    assert(capture.bufferedStates[2].blend == BlendMode::Additive &&
           !capture.bufferedStates[2].writeDepth);
    assert(capture.bufferedStates[3].blend == BlendMode::Alpha &&
           !capture.bufferedStates[3].writeDepth);
    assert(capture.bufferedTransforms[2][14] == -3.0F);
    assert(capture.bufferedTransforms[3][14] == 1.0F);
    assert(capture.bufferedAlpha[3] == 0.25F);
    assert(capture.bufferedTint[3][0] == 0.15F);
    assert(capture.bufferedTint[3][1] == 0.175F);
    assert(capture.bufferedMetalRough[2][0] == 0.75F);
    assert(capture.bufferedMetalRough[3][1] == 0.25F);
    static_cast<void>(graphics.endFrame());
    surfaces.entities[0].component<MeshRendererComponent>()->roughness = 0.6F;
    capture.bufferedMetalRough.clear();
    assert(renderer.renderFrame(surfaces, surfaceFrame, 0.016F, error));
    assert(capture.bufferedMetalRough.back()[1] == 0.6F);
    static_cast<void>(graphics.endFrame());
  }
  assert(collectSceneLighting3D(world,{}).msaaSamples==4);
  assert(capture.views3D.back().frameBuffer);
  const auto defaultAaTarget=capture.views3D.back().frameBuffer;
  auto nativeFrame=frame;nativeFrame.destinationSamples=4;nativeFrame.camera.renderHud=false;
  capture.views3D.clear();capture.views2D.clear();
  assert(renderer.renderFrame(world,nativeFrame,.016F,error));
  auto overlayCamera=nativeFrame;overlayCamera.viewId+=CameraViewCount3D;
  overlayCamera.camera.clearMode="depth";
  assert(renderer.renderFrame(world,overlayCamera,.016F,error));
  assert(capture.views3D.size()==2 && !capture.views3D[0].frameBuffer && !capture.views3D[1].frameBuffer);
  assert(!capture.views3D[1].clearColor && capture.views3D[1].clearDepth);
  assert(capture.views2D.empty()); // Camera stacks keep the shared native color/depth surface.
  static_cast<void>(graphics.endFrame());
  Entity quality;quality.id="quality";
  quality.setComponent(Environment3DComponent{.msaaSamples=0});
  world.entities.push_back(std::move(quality));
  capture.views3D.clear();capture.views2D.clear();
  assert(renderer.renderFrame(world,frame,.016F,error));
  assert(!capture.views3D.back().frameBuffer);
  assert(!resources->destroy(defaultAaTarget)); // Disabled AA releases its attachments.
  assert(std::ranges::none_of(capture.views2D,[&](const auto &view){return view.id==frame.viewId+2;}));
  static_cast<void>(graphics.endFrame());
  world.entities.back().component<Environment3DComponent>()->msaaSamples=8;
  capture.views3D.clear();capture.views2D.clear();
  assert(renderer.renderFrame(world,frame,.016F,error));
  assert(capture.views3D.back().frameBuffer && capture.views3D.back().frameBuffer!=defaultAaTarget);
  assert(std::ranges::any_of(capture.views2D,[&](const auto &view){return view.id==frame.viewId+2;}));
  static_cast<void>(graphics.endFrame());
  world.entities.pop_back();
  {
    World skyWorld;
    Entity environment;
    environment.id = "environment";
    Environment3DComponent::parse({{"sky_texture", "asset://textures/override"}}, environment);
    assert(environment.component<Environment3DComponent>()->skyTexture == "asset://textures/override");
    skyWorld.entities.push_back(std::move(environment));
    auto skyFrame = frame;
    skyFrame.position = {13, 2, -17};
    skyFrame.lightingOverride = SceneLighting3D{}; // Editor lighting override preserves sky.
    capture.bufferedStates.clear();
    capture.bufferedTransforms.clear();
    assert(renderer.renderFrame(skyWorld, skyFrame, 0.016F, error));
    assert(capture.bufferedStates.size() == 1);
    assert(capture.bufferedStates.front().depthTest == DepthTest::LessEqual);
    assert(!capture.bufferedStates.front().writeDepth);
    assert(capture.bufferedTransforms.front()[12] == 13);
    assert(capture.bufferedTransforms.front()[14] == -17);
    static_cast<void>(graphics.endFrame());
    skyWorld.entities.front().enabled = false;
    capture.bufferedStates.clear();
    assert(renderer.renderFrame(skyWorld, skyFrame, 0.016F, error));
    assert(capture.bufferedStates.empty());
    static_cast<void>(graphics.endFrame());
    skyWorld.entities.front().enabled = true;
    skyFrame.camera.perspective = false;
    assert(renderer.renderFrame(skyWorld, skyFrame, 0.016F, error));
    assert(capture.bufferedStates.empty());
    static_cast<void>(graphics.endFrame());
    skyFrame.camera.perspective = true;
    skyFrame.camera.clearMode = "depth";
    assert(renderer.renderFrame(skyWorld, skyFrame, 0.016F, error));
    assert(capture.bufferedStates.empty());
    static_cast<void>(graphics.endFrame());
  }
  for (int index = 0; index < 64; ++index) {
    world.entities.push_back(shape("sphere_copy_" + std::to_string(index),
                                   "sphere", {0.0F, 0.0F, 0.0F}));
  }
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  assert(renderer.statistics().visibleMeshes == 70U);
  assert(renderer.statistics().batches == batchesWithoutDebugGeometry);
  static_cast<void>(graphics.endFrame());
  // The second frame reuses the resident procedural buffers. Changing the
  // revision replaces them, and removing the owner releases the cache entry.
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());

  world.entities.front().setComponent(BoxCollider3DComponent{});
  world.debug.colliders = true;
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  assert(renderer.statistics().batches == batchesWithoutDebugGeometry + 1U);
  static_cast<void>(graphics.endFrame());
  world.debug.colliders = false;

  Entity emitter;
  emitter.id = "particles";
  emitter.setComponent(Transform3DComponent{});
  emitter.setComponent(ParticleEmitter3DComponent{.burst = 3,
                                                  .lifetime = 2.0F,
                                                  .sizeStart = 0.5F,
                                                  .sizeEnd = 0.25F,
                                                  .playing = true,
                                                  .loop = false});
  world.entities.push_back(std::move(emitter));
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  assert(renderer.statistics().particles == 3);
  assert(renderer.statistics().triangles >= 6);
  static_cast<void>(graphics.endFrame());

  BgfxCameraFrame3D postProcessFrame = frame;
  postProcessFrame.postProcess =
      PostProcessStackComponent{.exposure = 0.5F,
                                .contrast = 1.2F,
                                .saturation = 0.7F,
                                .tint = {0.9F, 0.8F, 1.0F, 1.0F},
                                .vignette = 0.4F,
                                .bloom = 0.5F,
                                .bloomThreshold = 0.8F,
                                .fadeColor = {0.1F, 0.2F, 0.3F, 0.8F},
                                .fade = 0.25F};
  assert(renderer.renderFrame(world, postProcessFrame, 0.016F, error));
  assert(renderer.statistics().batches >= 2);
  static_cast<void>(graphics.endFrame());

  const RenderTargetHandles embeddedTarget = resources->createRenderTarget(
      {.width = 320, .height = 180, .debugName = "embedded 3D test"}, error);
  assert(embeddedTarget.frameBuffer && embeddedTarget.color);
  BgfxCameraFrame3D embeddedFrame = postProcessFrame;
  embeddedFrame.frameBuffer = embeddedTarget.frameBuffer;
  embeddedFrame.camera.renderHudToTarget = true;
  assert(renderer.renderFrame(world, embeddedFrame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  for (const float scale : {0.8F, 1.5F}) {
    embeddedFrame.postProcess.reset();
    embeddedFrame.camera.renderScale = scale;
    capture.views2D.clear();
    capture.views3D.clear();
    assert(renderer.renderFrame(world, embeddedFrame, 0.016F, error));
    assert(capture.views3D.front().width == std::lround(320 * scale));
    assert(capture.views3D.front().frameBuffer != embeddedTarget.frameBuffer);
    const auto present = std::ranges::find_if(capture.views2D, [&](const auto &view) {
      return view.id == embeddedFrame.viewId + 2;
    });
    assert(present != capture.views2D.end());
    assert(present->frameBuffer == embeddedTarget.frameBuffer);
    assert(present->width == 320 && present->height == 180);
    assert(present->x == 0 && present->y == 0);
    static_cast<void>(graphics.endFrame());
  }
  assert(resources->destroy(embeddedTarget.frameBuffer));
  assert(resources->destroy(embeddedTarget.depth));
  assert(resources->destroy(embeddedTarget.color));

  Entity sun;sun.id="shadow-sun";
  sun.setComponent(DirectionalLightComponent{.castsShadows=true});
  sun.setComponent(Environment3DComponent{.shadowCascades = 1});
  world.entities.push_back(std::move(sun));
  capture.views3D.clear();capture.transformsByView.clear();capture.shadowTextures.clear();
  assert(renderer.renderFrame(world,frame,.016F,error));
  assert(capture.views3D.size()==2 && capture.views3D[0].frameBuffer && capture.views3D[1].frameBuffer);
  assert(capture.views3D[0].frameBuffer!=capture.views3D[1].frameBuffer);
  assert(capture.views3D[0].id==frame.viewId && capture.views3D[1].id==frame.viewId+1);
  assert(capture.views3D[0].width==1024 && !capture.views3D[0].perspective);
  assert(capture.views3D[0].clearColor && capture.views3D[0].clearDepth && capture.views3D[0].clearRgba==0xffffffffU);
  assert(capture.shadowTextures[frame.viewId]!=capture.shadowTextures[frame.viewId+1]);
  const auto firstShadowTexture=capture.shadowTextures[frame.viewId+1];
  const auto secondTarget=resources->createRenderTarget({.width=320,.height=180,.debugName="second shadow camera"},error);
  auto secondCamera=frame;secondCamera.cameraId="second";secondCamera.viewId=CameraViewCount3D;
  secondCamera.frameBuffer=secondTarget.frameBuffer;
  assert(renderer.renderFrame(world,secondCamera,.016F,error));
  assert(capture.shadowTextures[secondCamera.viewId+1]!=firstShadowTexture);
  assert(capture.views3D.back().frameBuffer!=secondTarget.frameBuffer);
  assert(std::ranges::any_of(capture.views2D,[&](const auto &v){return v.frameBuffer==secondTarget.frameBuffer && v.id==secondCamera.viewId+3;}));
  const auto previousShadow=capture.transformsByView[frame.viewId];
  assert(!previousShadow.empty());
  static_cast<void>(graphics.endFrame());
  assert(resources->destroy(secondTarget.frameBuffer));
  assert(resources->destroy(secondTarget.depth));
  assert(resources->destroy(secondTarget.color));
  world.entities.front().component<Transform3DComponent>()->position.x+=.5F;
  capture.transformsByView.clear();
  assert(renderer.renderFrame(world,frame,.016F,error));
  assert(capture.transformsByView[frame.viewId]!=previousShadow);
  static_cast<void>(graphics.endFrame());
  world.entities.front().component<Transform3DComponent>()->position.x-=.5F;
  Entity shadowSettings;shadowSettings.id="shadow-settings";
  shadowSettings.setComponent(Environment3DComponent{.maxShadowLights=0});
  world.entities.push_back(std::move(shadowSettings));
  capture.views3D.clear();
  assert(renderer.renderFrame(world,frame,.016F,error));
  assert(capture.views3D.size()==1);
  static_cast<void>(graphics.endFrame());
  world.entities.back().component<Environment3DComponent>()->maxShadowLights=1;
  world.entities.back().component<Environment3DComponent>()->shadowResolution=512;
  capture.views3D.clear();
  assert(renderer.renderFrame(world,frame,.016F,error));
  assert(capture.views3D.size() == 5 && capture.views3D[0].width == 512);
  const auto atlas = capture.views3D[0].frameBuffer;
  for (int cascade = 0; cascade < 4; ++cascade) {
    assert(capture.views3D[cascade].frameBuffer == atlas);
    assert(capture.views3D[cascade].id == frame.viewId + cascade);
    assert(capture.views3D[cascade].x == (cascade % 2) * 512);
    assert(capture.views3D[cascade].y == (cascade / 2) * 512);
    assert(capture.views3D[cascade].width == 512 &&
           capture.views3D[cascade].height == 512);
  }
  assert(capture.views3D[4].id == frame.viewId + 4);
  assert(capture.views3D[0].orthographicSize <
         capture.views3D[3].orthographicSize);
  static_cast<void>(graphics.endFrame());
  world.entities.back().component<Environment3DComponent>()->shadowCascades = 3;
  capture.views3D.clear();
  assert(renderer.renderFrame(world, frame, .016F, error));
  assert(capture.views3D.size() == 4);
  assert(capture.views3D[2].x == 0 && capture.views3D[2].y == 512);
  assert(capture.views3D[3].id == frame.viewId + 3);
  static_cast<void>(graphics.endFrame());
  world.entities.pop_back();
  world.entities.pop_back();
  capture.views3D.clear();
  assert(renderer.renderFrame(world,frame,.016F,error));
  assert(capture.views3D.size()==1);
  static_cast<void>(graphics.endFrame());

  BgfxCameraFrame3D targetFrame = frame;
  targetFrame.cameraId = "minimap";
  targetFrame.camera.renderTarget = "asset://targets/test";
  targetFrame.camera.renderHudToTarget = true;
  assert(renderer.renderFrame(world, targetFrame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  // A throttled target keeps presenting its resident color attachment without
  // clearing or redrawing the world.
  targetFrame.updateContent = false;
  assert(renderer.renderFrame(world, targetFrame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  Entity targetQuality;targetQuality.id="target-quality";
  targetQuality.setComponent(Environment3DComponent{.msaaSamples=0});
  world.entities.push_back(std::move(targetQuality));
  targetFrame.updateContent=true;
  assert(renderer.renderFrame(world,targetFrame,.016F,error));
  static_cast<void>(graphics.endFrame());
  world.entities.back().component<Environment3DComponent>()->msaaSamples=8;
  assert(renderer.renderFrame(world,targetFrame,.016F,error));
  static_cast<void>(graphics.endFrame());
  world.entities.pop_back();

  BgfxCameraFrame3D missingTargetFrame = targetFrame;
  missingTargetFrame.camera.renderTarget = "asset://targets/missing";
  assert(!renderer.renderFrame(world, missingTargetFrame, 0.016F, error));
  assert(error.find("unloaded render target") != std::string::npos);

  // Reloading an empty registry releases every target and removes its UI
  // texture registration. A former target must not remain renderable through a
  // stale GPU handle.
  demi::AssetRegistry emptyRegistry;
  diagnostics.clear();
  assert(renderer.loadAssets(emptyRegistry, diagnostics));
  assert(!renderer.renderFrame(world, targetFrame, 0.016F, error));
  assert(error.find("unloaded render target") != std::string::npos);

  diagnostics.clear();
  demi::AssetRegistry invalidTargetRegistry;
  invalidTargetRegistry.assets.push_back(
      {.id = "asset://targets/invalid",
       .type = "RenderTarget",
       .sourcePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                     "examples/minimal_voxel/assets/targets/missing.json"});
  assert(!renderer.loadAssets(invalidTargetRegistry, diagnostics));
  assert(!diagnostics.empty());

  // Restore model assets before exercising skeletal animation below.
  diagnostics.clear();
  assert(renderer.loadAssets(registry, diagnostics));
  const auto proceduralEntity =
      std::ranges::find_if(world.entities, [](const Entity &entity) {
        return entity.id == "procedural";
      });
  assert(proceduralEntity != world.entities.end());
  proceduralEntity->component<MeshRendererComponent>()->revision = 2;
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  std::erase_if(world.entities,
                [](const Entity &entity) { return entity.id == "procedural"; });
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());

  Entity animated = shape("model", "", {});
  animated.component<MeshRendererComponent>()->model =
      "asset://models/animated";
  animated.setComponent(
      AnimationPlayer3DComponent{.clipName = "Walk_Loop", .time = 0.2F});
  world.entities.push_back(std::move(animated));
  diagnostics.clear();
  assert(diagnostics.empty());
  RuntimeProfiler::setEnabled(true);
  RuntimeProfiler::beginFrame();
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  const auto rebuilt = RuntimeProfiler::frameEntries();
  for (const std::string name :
       {"Renderer3D.animation_rebuild", "Renderer3D.skin_cpu",
        "Renderer3D.skin_upload_cpu"}) {
    assert(std::any_of(rebuilt.begin(), rebuilt.end(), [&](const auto &entry) {
      return entry.name == name && entry.calls == 1;
    }));
  }
  RuntimeProfiler::beginFrame();
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  const auto unchanged = RuntimeProfiler::frameEntries();
  assert(
      std::none_of(unchanged.begin(), unchanged.end(), [](const auto &entry) {
        return entry.name == "Renderer3D.animation_rebuild" && entry.calls > 0;
      }));
  world.entities.back().component<AnimationPlayer3DComponent>()->time = 0.4F;
  RuntimeProfiler::beginFrame();
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  const auto advanced = RuntimeProfiler::frameEntries();
  assert(std::any_of(advanced.begin(), advanced.end(), [](const auto &entry) {
    return entry.name == "Renderer3D.animation_rebuild" && entry.calls == 1;
  }));
  // More than two preparation batches, followed by a cached frozen frame.
  for (int i = 0; i < 40; ++i) {
    Entity peer = shape("peer_" + std::to_string(i), "", {});
    peer.component<MeshRendererComponent>()->model = "asset://models/animated";
    peer.setComponent(AnimationPlayer3DComponent{.clipName = "Walk_Loop", .time = float(i) * 0.01F});
    world.entities.push_back(std::move(peer));
  }
  RuntimeProfiler::beginFrame();
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  const auto crowd = RuntimeProfiler::frameEntries();
  assert(std::any_of(crowd.begin(), crowd.end(), [](const auto &entry) {
    return entry.name == "Renderer3D.animation_rebuild" && entry.calls == 40;
  }));
  assert(std::any_of(crowd.begin(), crowd.end(), [](const auto &entry) {
    return entry.name == "Renderer3D.animation_batch_meshes" && entry.gauge == 16;
  }));
  assert(std::any_of(crowd.begin(), crowd.end(), [](const auto &entry) {
    return entry.name == "Renderer3D.animation_batch_vertices" && entry.gauge > 0 && entry.gauge <= 262144;
  }));
  RuntimeProfiler::beginFrame();
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  const auto frozen = RuntimeProfiler::frameEntries();
  assert(std::none_of(frozen.begin(), frozen.end(), [](const auto &entry) {
    return entry.name == "Renderer3D.animation_rebuild" && entry.calls > 0;
  }));
  // Loop policy is part of the pose key even if time is unchanged.
  world.entities.back().component<AnimationPlayer3DComponent>()->loop = false;
  RuntimeProfiler::beginFrame();
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  const auto loopChanged = RuntimeProfiler::frameEntries();
  assert(std::any_of(loopChanged.begin(), loopChanged.end(), [](const auto &entry) {
    return entry.name == "Renderer3D.animation_rebuild" && entry.calls == 1;
  }));
  auto *badPose = world.entities.back().component<AnimationPlayer3DComponent>();
  badPose->boneSegments["missing_test_bone"] = {{0, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  ++badPose->proceduralPoseRevision;
  assert(!renderer.renderFrame(world, frame, 0.016F, error));
  assert(error.find("missing_test_bone") != std::string::npos);
  badPose->boneSegments.clear();
  ++badPose->proceduralPoseRevision;
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  diagnostics.clear();
  assert(renderer.loadAssets(registry, diagnostics));
  RuntimeProfiler::beginFrame();
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  const auto reloaded = RuntimeProfiler::frameEntries();
  assert(std::any_of(reloaded.begin(), reloaded.end(), [](const auto &entry) {
    return entry.name == "Renderer3D.animation_rebuild" && entry.calls == 41;
  }));
  World invalidShapeWorld;
  invalidShapeWorld.entities.push_back(shape("editable", "unfinished_shape", {}));
  assert(!renderer.renderFrame(invalidShapeWorld, frame, .016F, error));
  invalidShapeWorld.entities.front().component<MeshRendererComponent>()->shape = "sphere";
  error.clear();
  assert(renderer.renderFrame(invalidShapeWorld, frame, .016F, error));
  static_cast<void>(graphics.endFrame());

  auto lightingFrame = frame;
  for (int lightingCase = 0; lightingCase < 4; ++lightingCase) {
    lightingFrame.lightingOverride = SceneLighting3D{};
    if (lightingCase == 1) {
      lightingFrame.lightingOverride->lights.push_back({.range = 10});
    } else if (lightingCase == 2) {
      lightingFrame.lightingOverride->lights.push_back({.kind = SceneLightKind3D::Spot, .range = 10, .outerConeCos = .5F, .innerConeCos = .8F});
    }
    RuntimeProfiler::beginFrame();
    assert(renderer.renderFrame(world, lightingFrame, 0.016F, error));
    static_cast<void>(graphics.endFrame());
    const auto entries = RuntimeProfiler::frameEntries();
    const double expected = lightingCase == 0 || lightingCase == 3 ? 1.0 : 0.0;
    assert(std::any_of(entries.begin(), entries.end(), [expected](const auto &entry) {
      return entry.name == "Renderer3D.directional_shader" && entry.gauge == expected;
    }));
  }
  RuntimeProfiler::setEnabled(false);

  World textureWorld;
  auto probe = shape("texture-probe", "cube", {});
  probe.component<MeshRendererComponent>()->texture = "asset://textures/override";
  textureWorld.entities.push_back(std::move(probe));
  capture.textures.clear();
  assert(renderer.renderFrame(textureWorld, frame, .016F, error));
  static_cast<void>(graphics.endFrame());
  assert(capture.textures.size() == 1);
  const auto explicitTexture = capture.textures.front();
  auto *mesh = textureWorld.entities.front().component<MeshRendererComponent>();
  mesh->texture.clear();
  capture.textures.clear();
  assert(renderer.renderFrame(textureWorld, frame, .016F, error));
  static_cast<void>(graphics.endFrame());
  assert(capture.textures.size() == 1 && capture.textures.front() != explicitTexture);
  // Static models go through instancing; animated ones use buffered submission.
  // Both must honor the same explicit texture as a primitive, not white/embedded.
  mesh->model = "asset://models/animated";
  mesh->texture = "asset://textures/override";
  for (bool animated : {false, true}) {
    if (animated)
      textureWorld.entities.front().setComponent(AnimationPlayer3DComponent{
          .clipName = "Walk_Loop", .time = .2F});
    capture.textures.clear();
    assert(renderer.renderFrame(textureWorld, frame, .016F, error));
    static_cast<void>(graphics.endFrame());
    assert(!capture.textures.empty());
    for (const auto texture : capture.textures)
      assert(texture == explicitTexture);
  }

  // Relief geometry is generated from an image in a shared session cache,
  // not imported from a generated per-brick asset.
  textureWorld.entities.front().removeComponent<AnimationPlayer3DComponent>();
  mesh=textureWorld.entities.front().component<MeshRendererComponent>();
  mesh->model.clear();
  SurfaceRelief3DComponent relief{.heightMap="asset://textures/override"};
  textureWorld.entities.front().setComponent(relief);
  capture.textures.clear();
  assert(renderer.renderFrame(textureWorld,frame,.016F,error));
  static_cast<void>(graphics.endFrame());
  assert(capture.textures.size()==1 && capture.textures.front()==explicitTexture);
  ReliefMeshCache3D cache(*resources);
  cache.loadAssets(registry);
  const auto *generated=cache.get(relief,{1,1,.115F},error);
  assert(generated && generated->indexCount()>36 && cache.size()==1);
  assert(cache.get(relief,{2,3,.115F},error)==generated && cache.size()==1);
  auto regionRelief=relief;
  regionRelief.tiles={4,3};regionRelief.atlasGrid={8,14};
  const auto *regionMesh=cache.get(regionRelief,{4,3,.115F},error);
  assert(regionMesh && regionMesh->indexCount()==12*generated->indexCount());
  assert(cache.get(regionRelief,{8,6,.115F},error)==regionMesh);
  auto flatRegion=regionRelief;flatRegion.heightMap.clear();flatRegion.depth=0;
  assert(cache.get(flatRegion,{4,3,.115F},error)->indexCount()==12*36);
  assert(!cache.get(relief,{1,1,.001F},error));
  cache.clear();
  assert(cache.size()==0);
  cache.loadAssets(registry);
  auto tile=relief;tile.uvScale={1.F/16,1.F/16};
  for(int i=0;i<300;++i) {
    tile.uvOffset={float(i%16)/16,float(i/16)/16};
    assert(cache.get(tile,{1,1,.115F},error));
  }
  assert(cache.get(relief,{1,1,.115F},error));
  assert(cache.size()>256); // An active view can exceed the retention budget.
  cache.beginFrame();
  cache.beginFrame();
  relief.uvOffset.y=.731F;
  assert(cache.get(relief,{1,1,.115F},error) && cache.size()==256);
  cache.clear();

  {
    auto project=loadProject(std::filesystem::path(DEMI_SOURCE_DIR)/"examples/destruction_3d_lab/demi.project.json",error);
    assert(project);
    auto &debrisWorld=project->world;
    FractureDebris3DComponent effect;
    effect.count=4; effect.lifetime=2; effect.fadeDuration=1;
    effect.speed=0; effect.gravity={};
    findEntity(debrisWorld,"arch")->setComponent(effect);
    auto &physics=ensurePhysicsWorld3D(debrisWorld);
    physics.step(debrisWorld,1.F/60);
    std::size_t affected=0;
    assert(debrisWorld.destruction3D->impact(debrisWorld,physics,
      {.position={-1.5F,1.5F,.49F},.radius=.2F,.energy=1,.entity="arch"},affected,error));
    assert(debrisWorld.destruction3D->cosmeticFragments().size()==4);
    debrisWorld.destruction3D->updateCosmetics(debrisWorld,1.5F);
    capture.bufferedStates.clear(); capture.bufferedAlpha.clear();
    assert(renderer.renderFrame(debrisWorld,frame,.016F,error));
    assert(capture.views3D.back().sequential);
    assert(capture.bufferedStates.size()>=4);
    for(std::size_t i=capture.bufferedStates.size()-4;i<capture.bufferedStates.size();++i) {
      assert(capture.bufferedStates[i].blend==BlendMode::Alpha && !capture.bufferedStates[i].writeDepth);
      assert(std::abs(capture.bufferedAlpha[i]-.5F)<1e-5F);
    }
    (void)graphics.endFrame();
    debrisWorld.destruction3D->updateCosmetics(debrisWorld,1);
    assert(renderer.renderFrame(debrisWorld,frame,.016F,error));
    assert(!capture.views3D.back().sequential);
    (void)graphics.endFrame();
  }
  for(int kind=0;kind<3;++kind) {
    World fading;
    Entity fragment;fragment.id="actual_fragment";
    fragment.setComponent(Transform3DComponent{});
    MeshRendererComponent mesh;
    mesh.color={1,1,1,1};
    if(kind==1) mesh.model="asset://models/lod_high";
    fragment.setComponent(mesh);
    if(kind==2) {
      SurfaceRelief3DComponent surface;surface.tiles={2,2};
      fragment.setComponent(surface);
    }
    fragment.setComponent(FragmentOpacity3D{.value=.5F});
    fading.entities.push_back(fragment);
    capture.bufferedStates.clear();capture.bufferedAlpha.clear();
    assert(renderer.renderFrame(fading,frame,.016F,error));
    assert(capture.views3D.back().sequential);
    assert(!capture.bufferedStates.empty());
    assert(capture.bufferedStates.back().blend==BlendMode::Alpha && !capture.bufferedStates.back().writeDepth);
    assert(std::abs(capture.bufferedAlpha.back()-.5F)<1e-5F);
    (void)graphics.endFrame();
  }
  {
    World instanceWorld;
    auto owner = shape("instance-owner", "cube", {100.0F, 0.0F, 0.0F});
    auto *mesh = owner.component<MeshRendererComponent>();
    mesh->hasBounds = true;
    mesh->boundsMin = {-0.5F, -0.5F, -0.5F};
    mesh->boundsMax = {0.5F, 0.5F, 0.5F};

    MeshInstances3DComponent instances;
    instances.transforms["left"].position = {-101.0F, 0.0F, 0.0F};
    instances.transforms["right"].position = {-99.0F, 0.0F, 0.0F};
    instances.transforms["outside"].position = {100.0F, 0.0F, 0.0F};
    owner.setComponent(instances);
    instanceWorld.entities.push_back(owner);

    const auto visible = extractVisibleMeshes3D(instanceWorld, frame);
    assert(visible.considered == 3 && visible.culled == 1 && visible.meshes.size() == 2);
    capture.transformsByView.clear();
    capture.instanceDrawCalls = 0;
    assert(renderer.renderFrame(instanceWorld, frame, 0.016F, error));
    assert(capture.transformsByView.at(frame.viewId).size() == 2);
    assert(capture.instanceDrawCalls == 1);
    (void)graphics.endFrame();

    instanceWorld.entities.front().component<MeshInstances3DComponent>()->transforms.clear();
    assert(extractVisibleMeshes3D(instanceWorld, frame).meshes.empty());
  }
  {
    MeshUploadCapture uploads(*resources);
    BgfxRenderer3D terrainRenderer(uploads, *commands);
    assert(terrainRenderer.initialize(error));
    World terrainWorld;
    Entity owner;
    owner.id = "terrain-owner";
    owner.setComponent(Transform3DComponent{});
    owner.setComponent(Terrain3DComponent{});
    terrainWorld.entities.push_back(std::move(owner));
    auto surface = shape("terrain-surface", "", {});
    auto *full = surface.component<MeshRendererComponent>();
    const std::array<Vec3, 3> triangle{{{-1, 0, 0}, {1, 0, 0}, {0, 1, 0}}};
    for (int copy = 0; copy < 4; ++copy) {
      full->vertices.insert(full->vertices.end(), triangle.begin(), triangle.end());
    }
    full->markGeometryChanged();
    const auto fullRevision = full->revision;
    auto chunk = std::make_shared<PreparedTerrainChunkLod>();
    chunk->boundsMin = {-1, -1, -1};
    chunk->boundsMax = {1, 1, 1};
    chunk->geometricErrors = {0, 0.1F, 0.2F, 0.4F};
    chunk->triangleCounts = {4, 3, 2, 1};
    for (std::size_t level = 1; level < 4; ++level) {
      auto geometry = std::make_shared<MeshRendererComponent>();
      for (std::size_t copy = 0; copy < 4 - level; ++copy) {
        geometry->vertices.insert(geometry->vertices.end(), triangle.begin(), triangle.end());
      }
      geometry->markGeometryChanged();
      chunk->levels[level]["grass"].geometry = std::move(geometry);
    }
    surface.setComponent(TerrainRenderLod{chunk, "grass", "terrain-owner"});
    terrainWorld.entities.push_back(std::move(surface));
    auto nearCamera = frame;
    nearCamera.cameraId = "near-camera";
    nearCamera.camera.renderHud = false;
    auto farCamera = nearCamera;
    farCamera.cameraId = "far-camera";
    farCamera.position = {0, 0, 500};
    farCamera.forward = {0, 0, -1};
    farCamera.camera.farClip = 1000;
    assert(terrainRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
    assert(terrainRenderer.terrainStatistics().selectedSurfaces[3] == 1);
    assert(terrainRenderer.terrainStatistics().triangles == 1);
    assert(terrainRenderer.terrainStatistics().uploads == 1);
    static_cast<void>(graphics.endFrame());
    assert(terrainRenderer.renderFrame(terrainWorld, nearCamera, 0.016F, error));
    assert(terrainRenderer.terrainStatistics().selectedSurfaces[0] == 1);
    assert(terrainRenderer.terrainStatistics().triangles == 4);
    assert(terrainRenderer.terrainStatistics().uploads == 1);
    static_cast<void>(graphics.endFrame());
    assert(terrainWorld.entities[1].component<MeshRendererComponent>()->revision == fullRevision);
    assert(terrainWorld.entities[1].component<MeshRendererComponent>()->vertices.size() == 12);

    // Multiple cameras retain the union of Full and reduced GPU demand.
    for (int presentation = 0; presentation < 10; ++presentation) {
      terrainRenderer.beginPresentation();
      assert(terrainRenderer.renderFrame(terrainWorld, nearCamera, 0.016F, error));
      assert(terrainRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
      terrainRenderer.endPresentation(true);
      assert(terrainRenderer.terrainStatistics().residentMeshes == 2);
      assert(terrainRenderer.terrainStatistics().uploads == 0);
      static_cast<void>(graphics.endFrame());
    }
    // The Full CPU mesh survives when its unused GPU variant is evicted.
    for (int presentation = 0; presentation < 10; ++presentation) {
      assert(terrainRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
      static_cast<void>(graphics.endFrame());
    }
    assert(terrainRenderer.terrainStatistics().residentMeshes == 1);
    assert(terrainRenderer.renderFrame(terrainWorld, nearCamera, 0.016F, error));
    assert(terrainRenderer.terrainStatistics().uploads == 1);
    static_cast<void>(graphics.endFrame());

    // Shadows use the originating camera's projection, not the cascade's.
    auto shadowCamera = farCamera;
    shadowCamera.camera.perspective = false;
    shadowCamera.camera.orthographicSize = 0.01F;
    shadowCamera.position = {0, 0, 5};
    const auto shadowDraws = extractVisibleMeshes3D(terrainWorld, shadowCamera,
                                                   nullptr, nullptr, &farCamera);
    assert(!shadowDraws.meshes.empty());
    for (const auto &draw : shadowDraws.meshes) {
      assert(draw.terrainLevel == 3);
    }

    // Reduced upload failure falls back to Full without changing the source.
    BgfxRenderer3D fallbackRenderer(uploads, *commands);
    assert(fallbackRenderer.initialize(error));
    uploads.failNextMeshUpload = true;
    assert(fallbackRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
    assert(fallbackRenderer.terrainStatistics().selectedSurfaces[0] == 1);
    assert(fallbackRenderer.terrainStatistics().triangles == 4);
    assert(error.empty());
    static_cast<void>(graphics.endFrame());
    assert(fallbackRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
    assert(fallbackRenderer.terrainStatistics().selectedSurfaces[3] == 1);
    static_cast<void>(graphics.endFrame());

    terrainWorld.entities.front().component<Terrain3DComponent>()->lodErrorPixels = 0;
    assert(terrainRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
    assert(terrainRenderer.terrainStatistics().selectedSurfaces[0] == 1);
    static_cast<void>(graphics.endFrame());

    Entity secondOwner;
    secondOwner.id = "second-terrain-owner";
    secondOwner.setComponent(Transform3DComponent{.position = {0, 0, -100}});
    secondOwner.setComponent(Terrain3DComponent{});
    auto secondSurface = terrainWorld.entities[1];
    secondSurface.id = "second-terrain-surface";
    secondSurface.setComponent(Transform3DComponent{.parent = secondOwner.id});
    secondSurface.setComponent(TerrainRenderLod{chunk, "grass", secondOwner.id});
    terrainWorld.entities.push_back(std::move(secondOwner));
    terrainWorld.entities.push_back(std::move(secondSurface));
    assert(terrainRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
    assert(terrainRenderer.terrainStatistics().selectedSurfaces[0] == 1);
    assert(terrainRenderer.terrainStatistics().selectedSurfaces[3] == 1);
    // Shared immutable reduced geometry uses the existing GPU variant even
    // though the placement, transform, quality and stable surface ID differ.
    assert(terrainRenderer.terrainStatistics().uploads == 0);
    assert(terrainRenderer.terrainStatistics().residentMeshes == 2);
    static_cast<void>(graphics.endFrame());
  }
  renderer.shutdown();
  renderer.shutdown();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
