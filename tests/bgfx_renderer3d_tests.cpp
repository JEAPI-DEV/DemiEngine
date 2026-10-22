#include "demi/runtime/destruction/DestructionWorld3D.h"
#include "demi/runtime/destruction/DetachedFragmentFade3D.h"
#include "demi/runtime/geometry/MeshDeformation3D.h"
#include "demi/runtime/physics/PhysicsWorld3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/BgfxRenderer3D.h"
#include "editor/EditorTerrainViewDemand.h"
#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"
#include "demi/runtime/render/bgfx3d/MeshDrawOrder3D.h"
#include "demi/runtime/render/bgfx3d/PreparedInstanceView3D.h"
#include "demi/runtime/render/bgfx3d/MeshInstanceBatch3D.h"
#include "demi/runtime/render/bgfx3d/MeshInstancePreparation3D.h"
#include "demi/runtime/render/bgfx3d/PreparedMeshDraw3D.h"
#include "demi/runtime/render/bgfx3d/PrimitiveSurfacePreparation3D.h"
#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/scene/SceneLoader.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/BoxCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Dentable3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/DirectionalLightComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Environment3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ParticleEmitter3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/SurfaceRelief3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Terrain3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"
#include "demi/runtime/scene/model/MeshMaterialSections3D.h"
#include "demi/runtime/terrain/TerrainMeshBuilder.h"
#include "demi/runtime/terrain/TerrainRenderLod.h"
#include "demi/runtime/terrain/TerrainWorld.h"
#include <atomic>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <thread>

#include <algorithm>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
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
  UniformHandle meshWindUniform;
  std::vector<bool> textureSrgbSampling;
  std::vector<std::string> textureNames;
  std::vector<TextureHandle> textureHandles;
  std::vector<TextureHandle> retiredTextures;
  std::thread::id uploadThread = std::this_thread::get_id();
  RenderDeviceLimits limits() const override { return target_.limits(); }
  TextureCapabilities textureCapabilities(TextureFormat format) const override {
    return target_.textureCapabilities(format);
  }
  TextureHandle createTexture(const TextureCreateInfo &info,
                              std::string &error) override {
    assert(std::this_thread::get_id() == uploadThread);
    textureSrgbSampling.push_back(info.srgb);
    textureNames.push_back(info.debugName);
    const auto handle = target_.createTexture(info, error);
    textureHandles.push_back(handle);
    return handle;
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
    const auto handle = target_.createUniform(name, type, count, error);
    if (name == "u_meshWind") {
      meshWindUniform = handle;
    }
    return handle;
  }
  BufferHandle createBuffer(const BufferCreateInfo &info,
                            std::string &error) override {
    assert(std::this_thread::get_id() == uploadThread);
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
    retiredTextures.push_back(handle);
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
  std::uint32_t availableInstances(std::uint16_t stride,
                                   std::uint32_t requested) const override {
    return target_.availableInstances(stride, requested);
  }
  std::vector<TextureHandle> textures;
  std::vector<DrawTextureBinding> lastTextureBindings;
  std::vector<std::vector<float>> lastUniformValues;
  std::vector<DrawState> bufferedStates;
  std::vector<BufferSlice> bufferedIndices;
  std::size_t instanceDrawCalls = 0;
  std::vector<DrawState> instanceStates;
  std::vector<std::array<float, 4>> instanceCutoffs;
  std::vector<std::array<float, 4>> lastInstanceUniforms;
  bool forwardDraws = true;
  const UniformHandle *meshWindUniform = nullptr;
  std::map<std::uint16_t, float> windTimeByView;
  std::vector<float> bufferedAlpha;
  std::vector<std::array<float, 4>> bufferedTint;
  std::vector<std::array<float, 4>> bufferedMetalRough;
  std::vector<std::array<float, 4>> batchDebugValues;
  std::vector<std::array<float, 16>> bufferedTransforms;
  std::vector<View2DConfig> views2D;
  std::vector<View3DConfig> views3D;
  std::map<std::uint16_t, std::vector<std::array<float, 16>>> transformsByView;
  std::map<std::uint16_t, TextureHandle> shadowTextures;
  std::map<std::uint16_t, std::array<float, 16>> shadowParameters;
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
    recordWindTime(draw.viewId, draw.uniforms);
    recordBatchDebugValues(draw.uniforms);
    lastTextureBindings.assign(draw.textures.begin(), draw.textures.end());
    lastUniformValues.clear();
    for (const auto &uniform : draw.uniforms) {
      lastUniformValues.emplace_back(uniform.values.begin(),
                                     uniform.values.end());
    }
    recordShadowParameters(draw.viewId, draw.uniforms);
    transformsByView[draw.viewId].push_back(draw.transform);
    for (const auto &texture : draw.textures)
      if (texture.stage == 1)
        shadowTextures[draw.viewId] = texture.texture;
    textures.push_back(draw.texture);
    bufferedStates.push_back(draw.state);
    bufferedIndices.push_back(draw.indices);
    bufferedAlpha.push_back(
        draw.uniforms.empty() ? 1.F : draw.uniforms.front().values[3]);
    if (!draw.uniforms.empty())
      bufferedTint.push_back(
          {draw.uniforms.front().values[0], draw.uniforms.front().values[1],
           draw.uniforms.front().values[2], draw.uniforms.front().values[3]});
    if (draw.uniforms.size() > 7)
      bufferedMetalRough.push_back(
          {draw.uniforms[7].values[0], draw.uniforms[7].values[1],
           draw.uniforms[7].values[2], draw.uniforms[7].values[3]});
    bufferedTransforms.push_back(draw.transform);
    return forwardDraws ? target_.submit(draw, error) : true;
  }
  bool submit(const InstancedBufferedDraw &draw, std::string &error) override {
    recordWindTime(draw.viewId, draw.uniforms);
    recordBatchDebugValues(draw.uniforms);
    recordShadowParameters(draw.viewId, draw.uniforms);
    ++instanceDrawCalls;
    instanceStates.push_back(draw.state);
    if (!draw.uniforms.empty() && draw.uniforms.back().values.size() == 4) {
      const auto values = draw.uniforms.back().values;
      lastInstanceUniforms.push_back({values[0], values[1], values[2], values[3]});
    }
    if (draw.uniforms.size() > 1) {
      const auto values = draw.uniforms[1].values;
      instanceCutoffs.push_back({values[0], values[1], values[2], values[3]});
    }
    if (draw.instances.stride == sizeof(std::array<float, 16>) &&
        draw.instances.data.size() % draw.instances.stride == 0) {
      for (std::size_t offset = 0; offset < draw.instances.data.size();
           offset += draw.instances.stride) {
        std::array<float, 16> transform;
        std::memcpy(transform.data(), draw.instances.data.data() + offset,
                    sizeof(transform));
        transformsByView[draw.viewId].push_back(transform);
      }
    }
    for (const auto &texture : draw.textures)
      if (texture.stage == 1)
        shadowTextures[draw.viewId] = texture.texture;
    textures.push_back(draw.texture);
    return forwardDraws ? target_.submit(draw, error) : true;
  }

private:
  void recordWindTime(std::uint16_t view,
                      std::span<const DrawUniformValue> uniforms) {
    if (!meshWindUniform) {
      return;
    }
    for (const auto &uniform : uniforms) {
      if (uniform.handle == *meshWindUniform && uniform.values.size() == 4) {
        windTimeByView[view] = uniform.values[3];
      }
    }
  }

  void recordBatchDebugValues(std::span<const DrawUniformValue> uniforms) {
    if (uniforms.size() < 3 || uniforms[2].values.size() != 4) {
      return;
    }
    std::array<float, 4> values;
    std::copy(uniforms[2].values.begin(), uniforms[2].values.end(),
              values.begin());
    batchDebugValues.push_back(values);
  }

  void recordShadowParameters(std::uint16_t view,
                              std::span<const DrawUniformValue> uniforms) {
    // DirectionalShadow3D interleaves each cascade array with a shared
    // uniform. Params is the penultimate entry, followed by Camera.
    if (uniforms.size() < 8) {
      return;
    }
    const auto parameters = uniforms[uniforms.size() - 2].values;
    if (parameters.size() != 16) {
      return;
    }
    auto &captured = shadowParameters[view];
    std::copy(parameters.begin(), parameters.end(), captured.begin());
  }
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
    PreparedMeshDraw3D custom(mesh, &material, std::nullopt, true, false, false,
                              {});
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
    assert(selectTerrainDrawLevel3D(*chunk, transform, {1, 1, 1}, camera,
                                    0.5F) == 3);
    transform.scale = {10, 1, 1};
    assert(selectTerrainDrawLevel3D(*chunk, transform, {1, 1, 1}, camera,
                                    0.5F) == 3);
    transform.scale = {1, 10, 1};
    assert(selectTerrainDrawLevel3D(*chunk, transform, {1, 1, 1}, camera,
                                    2.0F) == 2);
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
    assert(selection.select(chunk, transform, {1, 1, 1}, camera, 0.38F,
                            "owner-a") == 2);
    assert(selection.select(chunk, transform, {1, 1, 1}, camera, 0.42F,
                            "owner-a") == 2);
    assert(selection.select(chunk, transform, {1, 1, 1}, camera, 0.42F,
                            "owner-b") == 3);
    chunk->triangleCounts = {4, 4, 4, 4};
    assert(selectTerrainDrawLevel3D(*chunk, transform, {1, 1, 1}, camera,
                                    100) == 0);
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
        const float factor =
            float(camera.viewportHeight) / (2 * camera.camera.orthographicSize);
        return Vec2{x * factor, y * factor};
      }
      const float depth = camera.position.z - point.z;
      const float factor =
          float(camera.viewportHeight) /
          (2 * std::tan(camera.camera.fov * std::numbers::pi_v<float> / 360));
      return Vec2{x * factor / depth, y * factor / depth};
    };
    for (const bool perspective : {true, false}) {
      camera.camera.perspective = perspective;
      camera.camera.orthographicSize = 50;
      for (const Vec3 rotation :
           {Vec3{}, Vec3{35, 40, 25}, Vec3{80, 0, 10}, Vec3{0, 0, 90}}) {
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
                assert(std::hypot(full.x - reduced.x, full.y - reduced.y) <=
                       12.0F);
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
  {
    const auto materialPath = std::filesystem::temp_directory_path() /
        ("demi-surface-order-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    struct RemoveSurfaceFixture {
      std::filesystem::path path;
      ~RemoveSurfaceFixture() { std::filesystem::remove(path); }
    } cleanup{materialPath};
    const auto cutoutPath = std::filesystem::path(materialPath.string() + ".cutout.json");
    RemoveSurfaceFixture cutoutCleanup{cutoutPath};
    {
      std::ofstream output(cutoutPath);
      output << R"({"format_version":1,"render_state":{"blend":"opaque","alpha_cutoff":0.5}})";
    }
    {
      std::ofstream output(materialPath);
      output << R"({"format_version":1,"render_state":{"blend":"alpha"}})";
    }
    demi::AssetRegistry registry;
    registry.assets.push_back({.id = "asset://order/glass", .type = "Material",
                                .sourcePath = materialPath});
    registry.assets.push_back({.id = "asset://order/cutout", .type = "Material",
                                .sourcePath = cutoutPath});
    MaterialLibrary materials(*resources);
    std::vector<std::string> diagnostics;
    assert(materials.load(registry, diagnostics));
    assert(materials.find("asset://order/cutout")->alphaCutoff == 0.5F);
    auto foliage = shape("cutout-foliage", "cube", {});
    foliage.component<MeshRendererComponent>()->material = "asset://order/cutout";
    auto water = shape("blended-water", "cube", {});
    water.component<MeshRendererComponent>()->material = "asset://order/glass";
    const std::vector<VisibleMesh3D> mixedSurfaces{
        {.entity = &foliage, .transform = {.position = {0, 0, 9}}},
        {.entity = &water, .transform = {.position = {0, 0, 2}}},
        {.entity = &foliage, .transform = {.position = {0, 0, 1}}},
        {.entity = &water, .transform = {.position = {1, 0, 8}}},
        {.entity = &water, .transform = {.position = {-1, 0, 8}}}};
    auto colorSurfaces = mixedSurfaces;
    assert(orderMeshSurfaces3D(colorSurfaces, materials, {}, {0, 0, 1}) == 2);
    // Alpha-cutout foliage writes depth and keeps source order. Blended water
    // sorts back-to-front, retaining source order for equal-depth surfaces.
    assert(colorSurfaces[0].transform.position.z == 9);
    assert(colorSurfaces[1].transform.position.z == 1);
    assert(colorSurfaces[2].transform.position.x == 1);
    assert(colorSurfaces[3].transform.position.x == -1);
    assert(colorSurfaces[4].transform.position.z == 2);
    auto depthSurfaces = mixedSurfaces;
    assert(orderMeshSurfaces3D(depthSurfaces, materials, {}, {0, 0, 1},
                               MeshSurfacePass3D::OpaqueDepth) == 2);
    assert(depthSurfaces.size() == 2);
    assert(depthSurfaces[0].entity == &foliage);
    assert(depthSurfaces[1].entity == &foliage);
    assert(depthSurfaces[0].transform.position.z == 9);
    assert(depthSurfaces[1].transform.position.z == 1);

    World pairedWorld;
    MeshInstances3DComponent oneCopy;
    oneCopy.setTransform("copy", {.position = {0, 0, 5}});
    foliage.setComponent(oneCopy);
    water.setComponent(oneCopy);
    pairedWorld.entities.push_back(foliage);
    pairedWorld.entities.push_back(water);
    MeshInstancePreparation3D pairedPreparation;
    PreparedInstanceView3D pairedView;
    BgfxCameraFrame3D pairedCamera;
    pairedCamera.viewportWidth = 320;
    pairedCamera.viewportHeight = 180;
    const auto extractPaired = [&] {
      return extractVisibleMeshes3D(pairedWorld, pairedCamera, nullptr, nullptr,
                                    nullptr, nullptr, nullptr, nullptr,
                                    &materials, {}, &pairedPreparation, &pairedView);
    };
    pairedCamera.pass = BgfxCameraPass3D::OpaqueDepth;
    auto pairedDepth = extractPaired();
    assert(orderMeshSurfaces3D(pairedDepth.meshes, materials, {}, {0, 0, 1},
                               MeshSurfacePass3D::OpaqueDepth) == 1);
    assert(pairedDepth.meshes.size() == 1);
    pairedCamera.pass = BgfxCameraPass3D::Color;
    auto pairedColor = extractPaired();
    assert(pairedColor.instanceSourcesReused == 2);
    assert(pairedColor.meshes.size() == 2);
    assert(orderMeshSurfaces3D(pairedColor.meshes, materials, {}, {0, 0, 1}) == 1);
    assert(pairedColor.meshes[0].entity->id == "cutout-foliage");
    assert(pairedColor.meshes[1].entity->id == "blended-water");

    auto source = shape("surface-source", "cube", {});
    MeshMaterialSections3D sections;
    sections.sections = {{.id = "opaque"},
                          {.id = "glass", .material = "asset://order/glass"}};
    source.setComponent(sections);
    const auto copies = [&] {
      std::vector<VisibleMesh3D> meshes;
      for (int index = 0; index < 32; ++index) {
        const WorldTransform3D transform{.position = {0, 0, float(index)}};
        meshes.push_back({.entity = &source, .transform = transform,
                           .materialSection = 0});
        meshes.push_back({.entity = &source, .transform = transform,
                           .materialSection = 1});
      }
      return meshes;
    };
    auto meshes = copies();
    assert(orderMeshSurfaces3D(meshes, materials, {}, {0, 0, 1}) == 32);
    for (std::size_t index = 0; index < 32; ++index) {
      assert(meshes[index].materialSection == 0);
      assert(meshes[index].transform.position.z == float(index));
      assert(meshes[index + 32].materialSection == 1);
      assert(meshes[index + 32].transform.position.z == float(31 - index));
    }
    // Classification is call-local. Live overrides and destruction fades must
    // invalidate the previous source decision without a world revision signal.
    source.component<MeshRendererComponent>()->surfaceMode = "opaque";
    meshes = copies();
    std::ranges::reverse(meshes);
    assert(orderMeshSurfaces3D(meshes, materials, {}, {0, 0, 1}) == meshes.size());
    assert(meshes.front().transform.position.z == 31);
    assert(meshes.back().transform.position.z == 0);
    source.setComponent(FragmentOpacity3D{.value = .5F});
    meshes = copies();
    assert(orderMeshSurfaces3D(meshes, materials, {}, {0, 0, 1}) == 0);
    for (std::size_t index = 0; index < meshes.size(); index += 2) {
      assert(meshes[index].materialSection == 0);
      assert(meshes[index + 1].materialSection == 1);
    }

    World orderedWorld;
    orderedWorld.entities.push_back(shape("prepared-surface", "cube", {}));
    auto &orderedEntity = orderedWorld.entities.front();
    orderedEntity.setComponent(sections);
    MeshInstancePreparation3D preparation;
    const auto preparedCopies = [&](const MeshMaterialSections3D *terrain = nullptr) {
      const auto &prepared = preparation.worldSources(orderedWorld).front();
      std::vector<VisibleMesh3D> draws;
      for (int index = 0; index < 3; ++index) {
        for (std::size_t section = 0; section < 2; ++section) {
          draws.push_back({.entity = &orderedEntity,
                           .transform = {.position = {0, 0, float(index)}},
                           .materialSection = section,
                           .terrainSections = terrain,
                           .preparedSource = &prepared});
        }
      }
      return draws;
    };
    auto forwardDraws = preparedCopies();
    assert(orderMeshSurfaces3D(forwardDraws, materials, {}, {0, 0, 1}) == 3);
    assert(forwardDraws[0].transform.position.z == 0);
    assert(forwardDraws[2].transform.position.z == 2);
    assert(forwardDraws[3].transform.position.z == 2);
    // Opaque source order is camera-independent; transparency reverses depth.
    auto reverseDraws = preparedCopies();
    assert(orderMeshSurfaces3D(reverseDraws, materials, {}, {0, 0, -1}) == 3);
    assert(reverseDraws[0].transform.position.z == 0);
    assert(reverseDraws[2].transform.position.z == 2);
    assert(reverseDraws[3].transform.position.z == 0);
    auto preparedDepth = preparedCopies();
    std::ranges::reverse(preparedDepth);
    assert(orderMeshSurfaces3D(preparedDepth, materials, {}, {0, 0, 1},
                               MeshSurfacePass3D::OpaqueDepth) == 3);
    assert(preparedDepth.size() == 3);
    assert(preparedDepth[0].materialSection == 0);
    assert(preparedDepth[0].transform.position.z == 2);
    assert(preparedDepth[2].transform.position.z == 0);

    MeshMaterialSections3D terrainSections;
    terrainSections.sections = {
        {.id = "glass", .material = "asset://order/glass"}, {.id = "opaque"}};
    auto terrainDraws = preparedCopies(&terrainSections);
    assert(orderMeshSurfaces3D(terrainDraws, materials, {}, {0, 0, 1}) == 3);
    assert(terrainDraws.front().materialSection == 1);
    // A different selected terrain LOD must not inherit section-index decisions.
    auto originalSections = preparedCopies();
    assert(orderMeshSurfaces3D(originalSections, materials, {}, {0, 0, 1}) == 3);
    assert(originalSections.front().materialSection == 0);

    {
      std::ofstream output(materialPath);
      output << R"({"format_version":1,"render_state":{"blend":"opaque"}})";
    }
    diagnostics.clear();
    assert(materials.load(registry, diagnostics));
    preparation.reset();
    auto editedMaterial = preparedCopies();
    assert(orderMeshSurfaces3D(editedMaterial, materials, {}, {0, 0, 1}) == 6);

    orderedEntity.setComponent(FragmentOpacity3D{.value = .5F});
    preparation.reset();
    auto fadedDraws = preparedCopies();
    assert(orderMeshSurfaces3D(fadedDraws, materials, {}, {0, 0, 1}) == 0);
    assert(fadedDraws.front().transform.position.z == 2);
    assert(orderMeshSurfaces3D(fadedDraws, materials, {}, {0, 0, 1},
                               MeshSurfacePass3D::OpaqueDepth) == 0);
    assert(fadedDraws.empty());

    orderedEntity.removeComponent<FragmentOpacity3D>();
    preparation.reset();
    const auto unsectionedDraw = [&] {
      return std::vector<VisibleMesh3D>{
          {.entity = &orderedEntity,
           .preparedSource = &preparation.worldSources(orderedWorld).front()}};
    };
    auto opaqueDraw = unsectionedDraw();
    assert(orderMeshSurfaces3D(opaqueDraw, materials, {}, {0, 0, 1}) == 1);
    orderedEntity.component<MeshRendererComponent>()->surfaceMode = "transparent";
    preparation.reset();
    auto transparentDraw = unsectionedDraw();
    assert(orderMeshSurfaces3D(transparentDraw, materials, {}, {0, 0, 1}) == 0);
  }
  auto commands = createBgfxRenderCommands(*resources);
  {
    World sparseWorld;
    sparseWorld.entities.push_back(shape("sparse-mesh", "cube", {}));
    Entity nonMesh;
    nonMesh.id = "animation-without-mesh";
    nonMesh.setComponent(AnimationPlayer3DComponent{});
    sparseWorld.entities.push_back(std::move(nonMesh));
    MeshInstancePreparation3D preparation;
    const auto &initial = preparation.worldSources(sparseWorld);
    assert(initial[0].plainPrimitive);
    assert(!initial[1].animation);
    assert(!preparation.hasAnimations(sparseWorld));

    auto &entity = sparseWorld.entities.front();
    entity.setComponent(MeshInstances3DComponent{});
    entity.setComponent(MeshMaterialSections3D{});
    entity.setComponent(TerrainRenderLod{});
    entity.setComponent(AnimationPlayer3DComponent{});
    entity.setComponent(FragmentOpacity3D{.value = .4F});
    entity.setComponent(SurfaceRelief3DComponent{});
    Dentable3DComponent dentable;
    dentable.dents.push_back({.radius = 2});
    entity.setComponent(std::move(dentable));
    preparation.reset();
    const auto &populated = preparation.worldSources(sparseWorld);
    assert(populated.size() == 2);
    assert(populated[0].instances && populated[0].sections && populated[0].terrain);
    assert(populated[0].animation && populated[0].fade && populated[0].relief);
    assert(populated[0].dents.size() == 1);
    assert(populated[0].dents.front().radius == 2);
    assert(!populated[0].plainPrimitive);
    assert(preparation.hasAnimations(sparseWorld));

    entity.component<FragmentOpacity3D>()->value = .8F;
    entity.component<Dentable3DComponent>()->dents.front().radius = 3;
    preparation.reset();
    const auto &edited = preparation.worldSources(sparseWorld);
    assert(edited[0].fade->value == .8F);
    assert(edited[0].dents.front().radius == 3);

    entity.removeComponent<MeshInstances3DComponent>();
    entity.removeComponent<MeshMaterialSections3D>();
    entity.removeComponent<TerrainRenderLod>();
    entity.removeComponent<AnimationPlayer3DComponent>();
    entity.removeComponent<FragmentOpacity3D>();
    entity.removeComponent<SurfaceRelief3DComponent>();
    entity.removeComponent<Dentable3DComponent>();
    preparation.reset();
    const auto &removed = preparation.worldSources(sparseWorld);
    assert(!removed[0].instances && !removed[0].sections && !removed[0].terrain);
    assert(!removed[0].animation && !removed[0].fade && !removed[0].relief);
    assert(removed[0].dents.empty());
    assert(removed[0].plainPrimitive);
    assert(!preparation.hasAnimations(sparseWorld));

    entity.removeComponent<MeshRendererComponent>();
    preparation.reset();
    assert(!preparation.worldSources(sparseWorld)[0].mesh);
    entity.setComponent(MeshRendererComponent{});
    preparation.reset();
    assert(preparation.worldSources(sparseWorld)[0].mesh);
  }
  {
    World cachedWorld;
    auto parent = shape("cached-parent", "cube", {1, 0, 5});
    auto child = shape("cached-child", "cube", {2, 0, 0});
    child.updateComponent<Transform3DComponent>([&](auto &transform) { transform.parent = "cached-parent"; });
    child.component<MeshRendererComponent>()->model = "asset://cache/model";
    cachedWorld.entities.push_back(std::move(parent));
    cachedWorld.entities.push_back(std::move(child));
    BgfxCameraFrame3D cachedFrame;
    cachedFrame.forward = {0, 0, 1};
    cachedFrame.up = {0, 1, 0};
    cachedFrame.viewportWidth = 320;
    cachedFrame.viewportHeight = 180;
    cachedFrame.camera.perspective = false;
    cachedFrame.camera.orthographicSize = 20;
    cachedFrame.camera.farClip = 100;
    MeshInstancePreparation3D preparation;
    int boundsQueries = 0;
    const auto bounds = [&](std::string_view) -> std::optional<MeshBounds3D> {
      ++boundsQueries;
      return MeshBounds3D{{-.5F, -.5F, -.5F}, {.5F, .5F, .5F}};
    };
    const auto extract = [&](const BgfxCameraFrame3D &view) {
      return extractVisibleMeshes3D(
          cachedWorld, view, nullptr, nullptr, nullptr, nullptr, nullptr,
          nullptr, nullptr, bounds, &preparation);
    };
    const auto first = extract(cachedFrame);
    assert(first.meshes.size() == 2 && boundsQueries == 1);
    assert(first.meshes[1].transform.position.x == 3);
    const auto *matrix = first.meshes[1].preparedModelMatrix;
    assert(matrix != nullptr && (*matrix)[12] == 3);
    const auto second = extract(cachedFrame);
    assert(second.meshes.size() == 2 && boundsQueries == 1);
    assert(second.meshes[1].preparedModelMatrix == matrix);
    auto otherView = cachedFrame;
    otherView.position.x = 100;
    assert(extract(otherView).meshes.empty());
    assert(boundsQueries == 1);
    cachedWorld.entities[0].updateComponent<Transform3DComponent>([&](auto &transform) { transform.position.x = 4; });
    preparation.reset();
    const auto edited = extract(cachedFrame);
    assert(edited.meshes.size() == 2 && boundsQueries == 2);
    assert(edited.meshes[1].transform.position.x == 6);
    assert((*edited.meshes[1].preparedModelMatrix)[12] == 6);
    cachedWorld.entities[0].updateComponent<Transform3DComponent>([&](auto &transform) { transform.parent = "cached-child"; });
    preparation.reset();
    assert(extract(cachedFrame).meshes.empty());
    cachedWorld.entities[0].updateComponent<Transform3DComponent>([](auto &transform) { transform.parent.clear(); });
    cachedWorld.entities[1].updateComponent<Transform3DComponent>([&](auto &transform) { transform.parent = "missing"; });
    preparation.reset();
    assert(extract(cachedFrame).meshes.size() == 1);
  }
  {
    MeshUploadCapture uploads(*resources);
    TextureLibrary2D variants(uploads);
    ImageData2D image{.width = 2,
                      .height = 1,
                      .rgba = {std::byte{0}, std::byte{0}, std::byte{0},
                               std::byte{0}, std::byte{255}, std::byte{255},
                               std::byte{255}, std::byte{255}}};
    TextureSampling2D sampling;
    sampling.mipmaps = true;
    for (const auto semantic :
         {ImageMipSemantic::Color, ImageMipSemantic::Linear,
          ImageMipSemantic::Normal}) {
      sampling.mipSemantic = semantic;
      assert(variants.upload("shared-image", image, error, sampling));
    }
    // Color decoding must happen before bilinear/trilinear filtering in the
    // hardware, while data and UI views preserve their numerical channels.
    assert(uploads.textureSrgbSampling ==
           std::vector<bool>({true, false, false}));
    assert(variants.find("shared-image", ImageMipSemantic::Color).srgbDecoded);
    assert(
        !variants.find("shared-image", ImageMipSemantic::Linear).srgbDecoded);
    assert(
        !variants.find("shared-image", ImageMipSemantic::Normal).srgbDecoded);
  }

  {
    TextureCapture capture(*commands);
    capture.forwardDraws = false;
    std::vector<std::pair<std::string, ImageMipSemantic>> resolvedMaps;
    const TextureHandle mapTexture{.index = 7, .generation = 1};
    MeshPbrBindings3D bindings(
        *resources, capture,
        [&](std::string_view id, ImageMipSemantic semantic) {
          resolvedMaps.emplace_back(id, semantic);
          return TextureView2D{mapTexture, 4, 4,
                               semantic == ImageMipSemantic::Color};
        });
    MaterialBinding material;
    material.usesPbrSurface = true;
    material.surface.mapping = demi::assets::MaterialMapping::Triplanar;
    material.surface.tiling = 2;
    material.surface.normalMapYDown = true;
    material.surface.maps = {
        {demi::assets::MaterialMapSlot::Normal, "normal"},
        {demi::assets::MaterialMapSlot::Roughness, "roughness"},
        {demi::assets::MaterialMapSlot::Metallic, "metallic"},
        {demi::assets::MaterialMapSlot::Emissive, "emissive"}};
    MeshRendererComponent mesh;
    mesh.size = {12, 8, 4};
    assert(bindings.prepare(&material, mesh, {}, 3, error));
    assert(bindings.submit(BufferedDraw{}, error));
    assert(capture.lastTextureBindings.size() == 6);
    assert(capture.lastTextureBindings.front().stage == 3);
    assert(capture.lastTextureBindings.back().stage == 8);
    assert(capture.lastUniformValues[0] == std::vector<float>({1, 1, 1, 0}));
    assert(capture.lastUniformValues[1] == std::vector<float>({0, 1, 1, 0}));
    assert(capture.lastUniformValues[2][3] == -1);
    assert(capture.lastUniformValues[3] == std::vector<float>({12, 8, 4, 3}));
    assert(capture.lastUniformValues.size() == 5);
    assert(capture.lastUniformValues.back() == std::vector<float>({0, 1, 1, 0}));
    assert(resolvedMaps[0].second == ImageMipSemantic::Normal);
    assert(resolvedMaps[1].second == ImageMipSemantic::Linear);
    assert(resolvedMaps[3].second == ImageMipSemantic::Color);
    MeshPbrBindings3D targetBindings(
        *resources, capture, [&](std::string_view, ImageMipSemantic) {
          return TextureView2D{mapTexture, 4, 4, false};
        });
    assert(targetBindings.prepare(&material, mesh, {}, 1, error));
    assert(targetBindings.submit(BufferedDraw{}, error));
    assert(capture.lastUniformValues[1][3] == 1);
    // Presentation targets contain encoded RGB and request one shader decode;
    // their alpha and scalar-map channels retain their existing raw values.
    // Authored scalar overrides replace maps; they never multiply metallic
    // map values by the default metallic scalar of zero.
    mesh.roughness = 0.25F;
    mesh.materialNumbers["metallic"] = 0.7F;
    assert(bindings.prepare(&material, mesh, {}, 3, error));
    assert(bindings.submit(BufferedDraw{}, error));
    assert(capture.lastUniformValues[0][1] == 0);
    assert(capture.lastUniformValues[0][2] == 0);
    assert(!bindings.prepare(&material, mesh, {}, 0, error));
    MeshPbrBindings3D missingMaps(
        *resources, capture,
        [](std::string_view, ImageMipSemantic) { return TextureView2D{}; });
    assert(!missingMaps.prepare(&material, mesh, {}, 1, error));
    assert(error.find("normal") != std::string::npos);
    // An albedo-only material still needs the PBR projection path when an
    // entity overrides tiling; the renderer and adapter share this decision.
    MaterialBinding albedoOnly;
    albedoOnly.albedoTexture = "albedo";
    MeshRendererComponent tiledMesh;
    assert(!usesMeshPbrSurface3D(&albedoOnly, tiledMesh));
    tiledMesh.materialNumbers["tiling"] = 2;
    assert(usesMeshPbrSurface3D(&albedoOnly, tiledMesh));
    assert(bindings.prepare(&albedoOnly, tiledMesh, {}, 1, error));
    assert(bindings.submit(BufferedDraw{}, error));
    assert(capture.lastUniformValues[2][1] == 2);
    tiledMesh.materialNumbers["tiling"] = 0;
    assert(usesMeshPbrSurface3D(&albedoOnly, tiledMesh));
    assert(!bindings.prepare(&albedoOnly, tiledMesh, {}, 1, error));
    assert(error.find("positive") != std::string::npos);
    tiledMesh.materialNumbers["tiling"] =
        std::numeric_limits<float>::quiet_NaN();
    assert(usesMeshPbrSurface3D(&albedoOnly, tiledMesh));
    assert(!bindings.prepare(&albedoOnly, tiledMesh, {}, 1, error));
    albedoOnly.program = {.index = 1, .generation = 1};
    tiledMesh.materialNumbers["tiling"] = 2;
    assert(!usesMeshPbrSurface3D(&albedoOnly, tiledMesh));
    assert(bindings.prepare(&albedoOnly, tiledMesh, {}, 1, error));
    assert(bindings.submit(BufferedDraw{}, error));
    assert(capture.lastUniformValues ==
           std::vector<std::vector<float>>({{0, 0, 0, 0}}));
    // A following plain material must not inherit the preceding map slots.
    assert(bindings.prepare(nullptr, mesh, {}, 1, error));
    assert(bindings.submit(BufferedDraw{}, error));
    assert(capture.lastTextureBindings.empty());
    assert(capture.lastUniformValues ==
           std::vector<std::vector<float>>({{0, 0, 0, 0}}));
  }

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
    const std::array<float, 16> transform{1, 0, 0, 0, 0, 1, 0, 0,
                                          0, 0, 1, 0, 0, 0, 0, 1};
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
    assert(batches.flush(batchCapture, 0, {}, {}, {}, {}, lighting, draws,
                         triangles, error));
    assert(draws == 6);
    assert(triangles == 7);
    assert(batchCapture.instanceDrawCalls == 1);
    assert(batchCapture.textures.size() == 6);
    assert(batchCapture.textures[0] == texture);
    assert(batchCapture.textures[1] == texture);
    assert(batchCapture.textures[2] == recycled);
    assert(batchCapture.bufferedTint[2][0] == distinctTint[0]);
    assert(batchCapture.bufferedMetalRough.back()[1] == distinctMetalRough[1]);

    // Both the instanced group and the individual draws must retain the
    // camera direction used to encode eye-space depth. Grouping diagnostics
    // share this uniform, but must not overwrite another pass's payload.
    const std::array<float, 4> captureDirection{8.F, 0.25F, -23.F, -26.F};
    lighting[2].values = captureDirection;
    batchCapture.batchDebugValues.clear();
    assert(batches.flush(batchCapture, 0, {}, {}, {}, {}, lighting, draws,
                         triangles, error));
    assert(batchCapture.batchDebugValues.size() == 6);
    for (const auto &values : batchCapture.batchDebugValues) {
      assert(values == captureDirection);
    }

    const std::array<float, 4> groupingDebug{6.F, 0.F, 0.F, 0.F};
    lighting[2].values = groupingDebug;
    batchCapture.batchDebugValues.clear();
    assert(batches.flush(batchCapture, 0, {}, {}, {}, {}, lighting, draws,
                         triangles, error));
    assert(batchCapture.batchDebugValues.size() == 6);
    assert(batchCapture.batchDebugValues.front()[1] == 1.F);
    for (std::size_t index = 1; index < batchCapture.batchDebugValues.size();
         ++index) {
      assert(batchCapture.batchDebugValues[index] == groupingDebug);
    }
  }
  {
    // Inspect material batch intent with symbolic program/texture handles,
    // matching the material-free grouping probe above.
    TextureCapture surfaceCapture(*commands);
    surfaceCapture.forwardDraws = false;
    int resolvedSurfaceMaps = 0;
    MeshPbrBindings3D surfaceBindings(
        *resources, surfaceCapture,
        [&](std::string_view, ImageMipSemantic) {
          ++resolvedSurfaceMaps;
          return TextureView2D{TextureHandle{.index = 7, .generation = 1}, 4, 4, false};
        });
    GpuMesh3D geometry(*resources);
    const std::array<Vec3, 3> positions{{{-1, 0, 0}, {1, 0, 0}, {0, 1, 0}}};
    assert(geometry.upload(positions, {}, {}, 0xffffffffU, error));
    MeshRendererComponent source;
    MaterialBinding material;
    material.alphaCutoff = 0.4F;
    material.usesPbrSurface = true;
    material.surface.maps[demi::assets::MaterialMapSlot::Normal] = "normal";
    material.surface.windStrength = 0.2F;
    material.surface.windFrequency = 1.5F;
    material.surface.windHeight = 2.F;
    surfaceBindings.setFrameTime(3.F);
    material.state.cull = CullMode::Clockwise;
    material.state.writeDepth = true;
    PreparedMeshDraw3D prepared(source, &material, std::nullopt, false,
                                false, false, {});
    const std::array<float, 16> transform{1, 0, 0, 0, 0, 1, 0, 0,
                                         0, 0, 1, 0, 0, 0, 0, 1};
    MeshInstanceBatch3D batches;
    const auto surfaceGroup = batches.addSurface(
        geometry, {}, source, &material, prepared, true, transform);
    MeshRendererComponent anotherOwner = source;
    PreparedMeshDraw3D anotherPrepared(anotherOwner, &material, std::nullopt,
                                       false, false, false, {});
    assert(batches.addSurface(geometry, {}, anotherOwner, &material,
                              anotherPrepared, true, transform) == surfaceGroup);
    batches.appendTransform(surfaceGroup, transform);
    std::array<std::array<float, 4>, 8> values{};
    std::array<DrawUniformValue, 8> lighting;
    for (std::size_t index = 0; index < lighting.size(); ++index) {
      lighting[index].values = values[index];
    }
    std::uint32_t draws = 0;
    std::uint32_t triangles = 0;
    assert(batches.flush(surfaceCapture, 0, {}, {}, {}, {}, lighting, draws,
                         triangles, error, &surfaceBindings, {},
                         {.pbrMesh = {.index = 11, .generation = 1},
                          .pbrInstanced = {.index = 12, .generation = 1}}));
    assert(draws == 1 && triangles == 3);
    assert(resolvedSurfaceMaps == 1);
    assert(surfaceCapture.instanceDrawCalls == 1);
    assert(surfaceCapture.instanceStates.back().cull == CullMode::Clockwise);
    assert(surfaceCapture.instanceStates.back().writeDepth);
    assert(surfaceCapture.instanceCutoffs.back()[0] == 0.4F);

    MeshInstanceBatch3D distinctSurfaces;
    const auto baseGroup = distinctSurfaces.addSurface(
        geometry, {}, source, &material, prepared, true, transform);
    MeshRendererComponent tintedOwner = source;
    tintedOwner.color = {1, 0, 0, 1};
    PreparedMeshDraw3D tinted(tintedOwner, &material, std::nullopt, false,
                              false, false, {});
    assert(distinctSurfaces.addSurface(geometry, {}, tintedOwner, &material,
                                       tinted, {}, transform) != baseGroup);
    MeshRendererComponent tiledOwner = source;
    tiledOwner.materialNumbers["tiling"] = 2;
    PreparedMeshDraw3D tiled(tiledOwner, &material, std::nullopt, false,
                             false, false, {});
    assert(distinctSurfaces.addSurface(geometry, {}, tiledOwner, &material,
                                       tiled, {}, transform) != baseGroup);
    assert(surfaceCapture.lastInstanceUniforms.back() ==
           (std::array<float, 4>{0.2F, 1.5F, 2.F, 3.F}));

    // A subsequent material-free batch must clear the bending uniform rather
    // than inheriting the previous foliage surface's GPU state.
    MeshInstanceBatch3D plain;
    const std::array<float, 4> tint{1, 1, 1, 1};
    const std::array<float, 4> parameters{0, 0.8F, 0, 0};
    plain.add(geometry, {}, tint, parameters, false, transform);
    plain.add(geometry, {}, tint, parameters, false, transform);
    assert(plain.flush(surfaceCapture, 0, {}, {}, {}, {}, lighting, draws,
                       triangles, error, &surfaceBindings));
    assert(surfaceCapture.lastInstanceUniforms.back() ==
           (std::array<float, 4>{}));
  }
  {
    World primitives;
    primitives.entities.push_back(shape("first", "cube", {}));
    primitives.entities.push_back(shape("sized", "cube", {}));
    primitives.entities.back().component<MeshRendererComponent>()->size = {2, 3, 4};
    primitives.entities.push_back(shape("tinted", "cube", {}));
    primitives.entities.back().component<MeshRendererComponent>()->color.r = .25F;
    primitives.entities.push_back(shape("material", "cube", {}));
    primitives.entities.back().component<MeshRendererComponent>()->material = "asset://material";
    primitives.entities.push_back(shape("transparent", "cube", {}));
    primitives.entities.back().component<MeshRendererComponent>()->surfaceMode = "transparent";
    MeshInstancePreparation3D preparation;
    PrimitiveSurfacePreparation3D surfaces;
    const auto &sources = preparation.worldSources(primitives);
    assert(!preparation.hasAnimations(primitives));
    const auto first = surfaces.surfaceFor(sources[0]);
    assert(first && surfaces.surfaceFor(sources[1]) == first);
    assert(surfaces.surfaceFor(sources[0]) == first);
    assert(surfaces.surfaceFor(sources[2]) != first);
    assert(!surfaces.surfaceFor(sources[3]));
    assert(!surfaces.surfaceFor(sources[4]));
    assert(surfaces.size() == 2);
    primitives.entities[1].setComponent(AnimationPlayer3DComponent{});
    preparation.reset();
    surfaces.reset();
    const auto &updated = preparation.worldSources(primitives);
    assert(preparation.hasAnimations(primitives));
    assert(!surfaces.surfaceFor(updated[1]));
  }
  TextureCapture capture(*commands);
  BgfxRenderer3D renderer(*resources, capture,
                          false); // CPU fallback reference.
  assert(renderer.initialize(error));
  assert(renderer.initialize(error));
  std::vector<std::string> diagnostics;
  demi::AssetRegistry registry;
  registry.assets.push_back(
      {.id = "asset://textures/override",
       .type = "Texture2D",
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
  child.updateComponent<Transform3DComponent>([&](auto &transform) { transform.parent = "parent"; });
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
    TextureCapture lazyDraws(*commands);
    BgfxRenderer3D lazyRenderer(uploads, lazyDraws);
    assert(lazyRenderer.initialize(error));
    const std::string usedId = "asset://lazy/used";
    const std::string unusedId = "asset://lazy/unused";
    demi::AssetRegistry lazyRegistry;
    const auto imagePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                           "examples/minimal_3d/assets/textures/checker.png";
    for (const auto &id : {usedId, unusedId}) {
      lazyRegistry.assets.push_back(
          {.id = id, .type = "Texture2D", .sourcePath = imagePath});
    }
    const auto colorUploads = [&](const std::string &id) {
      std::size_t count = 0;
      for (std::size_t index = 0; index < uploads.textureNames.size();
           ++index) {
        if (uploads.textureNames[index] == id &&
            uploads.textureSrgbSampling[index]) {
          ++count;
        }
      }
      return count;
    };
    assert(lazyRenderer.loadAssets(lazyRegistry, diagnostics));
    assert(colorUploads(usedId) == 0 && colorUploads(unusedId) == 0);
    World lazyWorld;
    auto box = shape("lazy_mesh", "cube", {});
    box.component<MeshRendererComponent>()->texture = usedId;
    lazyWorld.entities.push_back(std::move(box));
    assert(lazyRenderer.renderFrame(lazyWorld, frame, 0.016F, error));
    static_cast<void>(graphics.endFrame());
    assert(colorUploads(usedId) == 1 && colorUploads(unusedId) == 0);
    assert(lazyRenderer.renderFrame(lazyWorld, frame, 0.016F, error));
    static_cast<void>(graphics.endFrame());
    assert(colorUploads(usedId) == 1);
    TextureHandle residentColor;
    for (std::size_t index = 0; index < uploads.textureNames.size(); ++index) {
      if (uploads.textureNames[index] == usedId &&
          uploads.textureSrgbSampling[index]) {
        residentColor = uploads.textureHandles[index];
      }
    }
    assert(residentColor);
    // Editor maintenance runs when a Game/Scene view skips even one UI frame.
    // Resuming must keep asset-owned textures rather than decode/upload again.
    for (unsigned skipped = 0; skipped < 3; ++skipped) {
      lazyRenderer.tickInactiveTerrain();
      assert(std::ranges::find(uploads.retiredTextures, residentColor) ==
             uploads.retiredTextures.end());
      lazyDraws.textures.clear();
      assert(lazyRenderer.renderFrame(lazyWorld, frame, 0.016F, error));
      static_cast<void>(graphics.endFrame());
      assert(colorUploads(usedId) == 1);
      assert(std::ranges::find(lazyDraws.textures, residentColor) !=
             lazyDraws.textures.end());
    }
    // Explicit asset replacement still retires and recreates its resources.
    assert(lazyRenderer.loadAssets(lazyRegistry, diagnostics));
    assert(std::ranges::find(uploads.retiredTextures, residentColor) !=
           uploads.retiredTextures.end());
    assert(colorUploads(usedId) == 1);
    assert(lazyRenderer.renderFrame(lazyWorld, frame, 0.016F, error));
    static_cast<void>(graphics.endFrame());
    assert(colorUploads(usedId) == 2 && colorUploads(unusedId) == 0);
  }

  {
    MeshUploadCapture uploads(*resources);
    TextureCapture stagedDraws(*commands);
    BgfxRenderer3D stagedRenderer(uploads, stagedDraws);
    assert(stagedRenderer.initialize(error));
    JobSystem preparationJobs(1);
    const auto imagePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                           "examples/minimal_3d/assets/textures/checker.png";
    const auto materialPath = std::filesystem::temp_directory_path() /
                              "demi_staged_material_test.material.json";
    {
      std::ofstream source(materialPath);
      source
          << R"({"format_version":1,"shader":"builtin://lit","textures":{"albedo":"asset://staged/albedo","normal":"asset://staged/normal"}})";
    }
    demi::AssetRegistry stagedRegistry;
    stagedRegistry.assets = {
        {.id = "asset://staged/albedo",
         .type = "Texture2D",
         .sourcePath = imagePath},
        {.id = "asset://staged/normal",
         .type = "Texture2D",
         .sourcePath = imagePath},
        {.id = "asset://staged/material",
         .type = "Material",
         .sourcePath = materialPath},
    };
    const auto stagedTextureCount = [&] {
      return std::ranges::count_if(uploads.textureNames, [](const auto &id) {
        return id.starts_with("asset://staged/");
      });
    };
    std::atomic_bool releaseWorker = false;
    auto blockedWorker = preparationJobs.submit([&] {
      while (!releaseWorker.load()) {
        std::this_thread::yield();
      }
    });
    assert(stagedRenderer.beginAssetPreparation(stagedRegistry, preparationJobs,
                                                diagnostics));
    assert(stagedRenderer.assetPreparationProgress() == 0.0);
    bool complete = false;
    for (int step = 0; step < 5; ++step) {
      assert(stagedRenderer.stepAssetPreparation(0.0, complete, diagnostics));
      assert(!complete);
    }
    assert(stagedTextureCount() == 0);
    assert(stagedRenderer.assetPreparationProgress() > 0.0);
    assert(stagedRenderer.assetPreparationProgress() < 1.0);
    std::atomic_bool cancelled = false;
    std::atomic_bool cancellationWaitedForWorker = false;
    std::thread releaseGuard([&] {
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(2);
      while (!cancelled.load() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      cancellationWaitedForWorker = !cancelled.load();
      releaseWorker = true;
    });
    stagedRenderer.cancelAssetPreparation();
    assert(stagedRenderer.assetPreparationProgress() == 0.0);
    cancelled = true;
    releaseGuard.join();
    blockedWorker.wait();
    assert(!cancellationWaitedForWorker.load());
    assert(stagedTextureCount() == 0);

    assert(stagedRenderer.beginAssetPreparation(stagedRegistry, preparationJobs,
                                                diagnostics));
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    double lastProgress = 0.0;
    while (!complete && std::chrono::steady_clock::now() < deadline) {
      assert(stagedRenderer.stepAssetPreparation(0.0, complete, diagnostics));
      if (!complete) {
        const double progress = stagedRenderer.assetPreparationProgress();
        assert(progress >= lastProgress && progress < 1.0);
        lastProgress = progress;
      }
      std::this_thread::yield();
    }
    assert(complete);
    assert(stagedTextureCount() == 2);
    for (std::size_t index = 0; index < uploads.textureNames.size(); ++index) {
      if (uploads.textureNames[index] == "asset://staged/albedo") {
        assert(uploads.textureSrgbSampling[index]);
      } else if (uploads.textureNames[index] == "asset://staged/normal") {
        assert(!uploads.textureSrgbSampling[index]);
      }
    }
    World stagedWorld;
    auto box = shape("staged_mesh", "cube", {});
    box.component<MeshRendererComponent>()->material =
        "asset://staged/material";
    stagedWorld.entities.push_back(std::move(box));
    for (int presentation = 0; presentation < 3; ++presentation) {
      assert(stagedRenderer.renderFrame(stagedWorld, frame, 0.016F, error));
      static_cast<void>(graphics.endFrame());
    }
    assert(stagedTextureCount() == 2);
    std::filesystem::remove(materialPath);
  }

  {
    MeshUploadCapture uploads(*resources);
    TextureCapture skyDraws(*commands);
    BgfxRenderer3D skyRenderer(uploads, skyDraws);
    assert(skyRenderer.initialize(error));
    JobSystem preparationJobs(1);
    const std::string skyId = "asset://sky/prewarm";
    demi::AssetManifest panorama;
    panorama.id = skyId;
    panorama.type = "Texture2D";
    panorama.sourcePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                          "examples/minimal_3d/assets/textures/checker.png";
    panorama.textureSettings.mipmaps = true;
    demi::AssetRegistry skyRegistry;
    skyRegistry.assets.push_back(std::move(panorama));
    assert(skyRenderer.beginAssetPreparation(skyRegistry, preparationJobs,
                                             diagnostics));
    bool complete = false;
    const auto assetsDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!complete && std::chrono::steady_clock::now() < assetsDeadline) {
      assert(skyRenderer.stepAssetPreparation(0.0, complete, diagnostics));
      std::this_thread::yield();
    }
    assert(complete);
    const auto variantCounts = [&] {
      std::array<std::size_t, 2> counts{};
      for (std::size_t index = 0; index < uploads.textureNames.size();
           ++index) {
        if (uploads.textureNames[index] == skyId) {
          ++counts[uploads.textureSrgbSampling[index] ? 0 : 1];
        }
      }
      return counts;
    };
    assert((variantCounts() == std::array<std::size_t, 2>{1, 0}));
    const auto skyWorld = [&] {
      World result;
      Entity environment;
      environment.id = "sky_environment";
      Environment3DComponent lighting;
      lighting.skyTexture = skyId;
      environment.setComponent(lighting);
      result.entities.push_back(std::move(environment));
      return result;
    };
    {
      World cancelled = skyWorld();
      assert(skyRenderer.prepareScenePresentation(cancelled, frame, complete,
                                                  error));
      assert(!complete);
      assert((variantCounts() == std::array<std::size_t, 2>{1, 0}));
      skyRenderer.cancelScenePresentationPreparation();
      skyRenderer.endPresentation(true);
    }
    World incoming = skyWorld();
    const auto warmupDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
      assert(skyRenderer.prepareScenePresentation(incoming, frame, complete,
                                                  error));
      skyRenderer.endPresentation(true);
      static_cast<void>(graphics.endFrame());
      std::this_thread::yield();
    } while (!complete && std::chrono::steady_clock::now() < warmupDeadline);
    assert(complete);
    assert((variantCounts() == std::array<std::size_t, 2>{1, 1}));
    assert(
        skyRenderer.prepareScenePresentation(incoming, frame, complete, error));
    assert(complete);
    skyRenderer.endPresentation(true);
    for (int presentation = 0; presentation < 3; ++presentation) {
      assert(skyRenderer.renderFrame(incoming, frame, 0.016F, error));
      static_cast<void>(graphics.endFrame());
    }
    assert((variantCounts() == std::array<std::size_t, 2>{1, 1}));
  }

  {
    MeshUploadCapture uploads(*resources);
    TextureCapture sectionDraws(*commands);
    BgfxRenderer3D sectionRenderer(uploads, sectionDraws);
    assert(sectionRenderer.initialize(error));

    World sectionWorld;
    auto panel = shape("sectioned_inline", "", {});
    auto *mesh = panel.component<MeshRendererComponent>();
    mesh->vertices = {{-1, 0, 0}, {0, 0, 0}, {-1, 1, 0},
                      {0, 0, 0},  {1, 0, 0}, {1, 1, 0}};
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
    retentionWorld.entities.front().updateComponent<Transform3DComponent>([&](auto &value) { value.position = {10000, 0, 0}; });
    assert(extractVisibleMeshes3D(retentionWorld, frame).meshes.empty());
    assert(retentionRenderer.renderFrame(retentionWorld, frame, 0.016F, error));
    static_cast<void>(graphics.endFrame());
    retentionWorld.entities.front().updateComponent<Transform3DComponent>([&](auto &value) { value.position = {}; });
    assert(retentionRenderer.renderFrame(retentionWorld, frame, 0.016F, error));
    assert(uploads.meshUploads.size() == 1);
    static_cast<void>(graphics.endFrame());

    // A failed frame cannot evict absent resources: the authoring document
    // may be restored immediately after an unsuccessful preview rebuild.
    Entity retained = std::move(retentionWorld.entities.front());
    retentionWorld.entities.clear();
    retentionWorld.entities.push_back(shape("invalid-preview", "invalid", {}));
    assert(
        !retentionRenderer.renderFrame(retentionWorld, frame, 0.016F, error));
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
    near.component<MeshRendererComponent>()->materialColors["base_color"] = {
        0.5F, 0.25F, 1.0F, 0.5F};
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
  {
    World qualityWorld;
    Entity environment;
    Environment3DComponent::parse({{"terrain_texture_max_size", 512},
                                   {"terrain_texture_cache_mb", 24},
                                   {"terrain_detail_distance", 75}},
                                  environment);
    qualityWorld.entities.push_back(std::move(environment));
    const auto lighting = collectSceneLighting3D(qualityWorld, {});
    assert(lighting.terrainTextureMaxSize == 512);
    assert(lighting.terrainTextureCacheBytes == 24U * 1024U * 1024U);
    assert(lighting.terrainDetailDistance == 75);
  }
  assert(collectSceneLighting3D(world, {}).msaaSamples == 4);
  assert(capture.views3D.back().frameBuffer);
  const auto defaultAaTarget = capture.views3D.back().frameBuffer;
  auto nativeFrame = frame;
  nativeFrame.destinationSamples = 4;
  nativeFrame.camera.renderHud = false;
  capture.views3D.clear();
  capture.views2D.clear();
  assert(renderer.renderFrame(world, nativeFrame, .016F, error));
  auto overlayCamera = nativeFrame;
  overlayCamera.viewId += CameraViewCount3D;
  overlayCamera.camera.clearMode = "depth";
  assert(renderer.renderFrame(world, overlayCamera, .016F, error));
  assert(capture.views3D.size() == 2 && !capture.views3D[0].frameBuffer &&
         !capture.views3D[1].frameBuffer);
  assert(!capture.views3D[1].clearColor && capture.views3D[1].clearDepth);
  assert(capture.views2D.empty()); // Camera stacks keep the shared native
                                   // color/depth surface.
  static_cast<void>(graphics.endFrame());
  Entity quality;
  quality.id = "quality";
  quality.setComponent(Environment3DComponent{.msaaSamples = 0});
  world.entities.push_back(std::move(quality));
  capture.views3D.clear();
  capture.views2D.clear();
  assert(renderer.renderFrame(world, frame, .016F, error));
  assert(!capture.views3D.back().frameBuffer);
  assert(!resources->destroy(
      defaultAaTarget)); // Disabled AA releases its attachments.
  assert(std::ranges::none_of(capture.views2D, [&](const auto &view) {
    return view.id == frame.viewId + 2;
  }));
  static_cast<void>(graphics.endFrame());
  world.entities.back().component<Environment3DComponent>()->msaaSamples = 8;
  capture.views3D.clear();
  capture.views2D.clear();
  assert(renderer.renderFrame(world, frame, .016F, error));
  assert(capture.views3D.back().frameBuffer &&
         capture.views3D.back().frameBuffer != defaultAaTarget);
  assert(std::ranges::any_of(capture.views2D, [&](const auto &view) {
    return view.id == frame.viewId + 2;
  }));
  static_cast<void>(graphics.endFrame());
  world.entities.pop_back();
  {
    World skyWorld;
    Entity environment;
    environment.id = "environment";
    Environment3DComponent::parse(
        {{"sky_texture", "asset://textures/override"}}, environment);
    assert(environment.component<Environment3DComponent>()->skyTexture ==
           "asset://textures/override");
    skyWorld.entities.push_back(std::move(environment));
    auto skyFrame = frame;
    skyFrame.position = {13, 2, -17};
    skyFrame.lightingOverride =
        SceneLighting3D{}; // Editor lighting override preserves sky.
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
    const auto present =
        std::ranges::find_if(capture.views2D, [&](const auto &view) {
          return view.id == embeddedFrame.viewId + 2;
        });
    assert(present != capture.views2D.end());
    assert(present->debugName == "PostProcess");
    assert(capture.views3D.front().debugName == "Main");
    assert(present->frameBuffer == embeddedTarget.frameBuffer);
    assert(present->width == 320 && present->height == 180);
    assert(present->x == 0 && present->y == 0);
    static_cast<void>(graphics.endFrame());
  }
  assert(resources->destroy(embeddedTarget.frameBuffer));
  assert(resources->destroy(embeddedTarget.depth));
  assert(resources->destroy(embeddedTarget.color));

  Entity sun;
  sun.id = "shadow-sun";
  sun.setComponent(DirectionalLightComponent{.castsShadows = true});
  sun.setComponent(Environment3DComponent{.shadowCascades = 1});
  world.entities.push_back(std::move(sun));
  capture.views3D.clear();
  capture.transformsByView.clear();
  capture.shadowTextures.clear();
  assert(renderer.renderFrame(world, frame, .016F, error));
  assert(capture.views3D.size() == 2 && capture.views3D[0].frameBuffer &&
         capture.views3D[1].frameBuffer);
  assert(capture.views3D[0].frameBuffer != capture.views3D[1].frameBuffer);
  assert(capture.views3D[0].id == frame.viewId &&
         capture.views3D[1].id == frame.viewId + 1);
  assert(capture.views3D[0].width == 1024 && !capture.views3D[0].perspective);
  assert(capture.views3D[0].clearColor && capture.views3D[0].clearDepth &&
         capture.views3D[0].clearRgba == 0xffffffffU);
  assert(capture.shadowTextures[frame.viewId] !=
         capture.shadowTextures[frame.viewId + 1]);
  const auto firstShadowTexture = capture.shadowTextures[frame.viewId + 1];
  const auto secondTarget = resources->createRenderTarget(
      {.width = 320, .height = 180, .debugName = "second shadow camera"},
      error);
  auto secondCamera = frame;
  secondCamera.cameraId = "second";
  secondCamera.viewId = CameraViewCount3D;
  secondCamera.frameBuffer = secondTarget.frameBuffer;
  assert(renderer.renderFrame(world, secondCamera, .016F, error));
  assert(capture.shadowTextures[secondCamera.viewId + 1] != firstShadowTexture);
  assert(capture.views3D.back().frameBuffer != secondTarget.frameBuffer);
  assert(std::ranges::any_of(capture.views2D, [&](const auto &v) {
    return v.frameBuffer == secondTarget.frameBuffer &&
           v.id == secondCamera.viewId + 3;
  }));
  const auto previousShadow = capture.transformsByView[frame.viewId];
  assert(!previousShadow.empty());
  static_cast<void>(graphics.endFrame());
  assert(resources->destroy(secondTarget.frameBuffer));
  assert(resources->destroy(secondTarget.depth));
  assert(resources->destroy(secondTarget.color));
  world.entities.front().updateComponent<Transform3DComponent>([&](auto &transform) { transform.position.x += .5F; });
  capture.transformsByView.clear();
  assert(renderer.renderFrame(world, frame, .016F, error));
  assert(capture.transformsByView[frame.viewId] != previousShadow);
  static_cast<void>(graphics.endFrame());
  world.entities.front().updateComponent<Transform3DComponent>([&](auto &transform) { transform.position.x -= .5F; });
  Entity shadowSettings;
  shadowSettings.id = "shadow-settings";
  shadowSettings.setComponent(Environment3DComponent{.maxShadowLights = 0});
  world.entities.push_back(std::move(shadowSettings));
  capture.views3D.clear();
  assert(renderer.renderFrame(world, frame, .016F, error));
  assert(capture.views3D.size() == 1);
  static_cast<void>(graphics.endFrame());
  world.entities.back().component<Environment3DComponent>()->maxShadowLights =
      1;
  world.entities.back().component<Environment3DComponent>()->shadowResolution =
      512;
  capture.views3D.clear();
  assert(renderer.renderFrame(world, frame, .016F, error));
  assert(capture.views3D.size() == 5 && capture.views3D[0].width == 512);
  const auto atlas = capture.views3D[0].frameBuffer;
  for (int cascade = 0; cascade < 4; ++cascade) {
    assert(capture.views3D[cascade].debugName == "Shadow");
    assert(capture.views3D[cascade].frameBuffer == atlas);
    assert(capture.views3D[cascade].id == frame.viewId + cascade);
    assert(capture.views3D[cascade].x == (cascade % 2) * 512);
    assert(capture.views3D[cascade].y == (cascade / 2) * 512);
    assert(capture.views3D[cascade].width == 512 &&
           capture.views3D[cascade].height == 512);
  }
  assert(capture.views3D[4].debugName == "Main");
  assert(capture.views3D[4].id == frame.viewId + 4);
  assert(capture.views3D[0].orthographicSize <
         capture.views3D[3].orthographicSize);
  static_cast<void>(graphics.endFrame());
  auto &shadowEnvironment =
      *world.entities.back().component<Environment3DComponent>();
  for (const int resolution : {512, 1024, 2048}) {
    shadowEnvironment.shadowResolution = resolution;
    capture.views3D.clear();
    capture.shadowParameters.clear();
    assert(renderer.renderFrame(world, frame, .016F, error));
    assert(capture.views3D.size() == 5);
    const auto receiverView = capture.views3D.back().id;
    const auto parameters = capture.shadowParameters.at(receiverView);
    for (std::size_t cascade = 0; cascade < 4; ++cascade) {
      const auto &caster = capture.views3D[cascade];
      assert(caster.width == resolution && !caster.perspective);
      const float worldTexel = caster.orthographicSize / caster.width;
      assert(worldTexel > 0);
      assert(parameters[cascade * 4 + 3] == worldTexel);
    }
    static_cast<void>(graphics.endFrame());
  }
  shadowEnvironment.shadowResolution = 512;
  shadowEnvironment.shadowCascades = 3;
  capture.views3D.clear();
  assert(renderer.renderFrame(world, frame, .016F, error));
  assert(capture.views3D.size() == 4);
  assert(capture.views3D[2].x == 0 && capture.views3D[2].y == 512);
  assert(capture.views3D[3].id == frame.viewId + 3);
  static_cast<void>(graphics.endFrame());
  world.entities.pop_back();
  world.entities.pop_back();
  capture.views3D.clear();
  assert(renderer.renderFrame(world, frame, .016F, error));
  assert(capture.views3D.size() == 1);
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

  // A cached target already contains its post-processing. A skipped update
  // must present that target directly, rather than process whichever camera
  // most recently wrote the shared scratch surface.
  targetFrame.updateContent = true;
  targetFrame.postProcess = PostProcessStackComponent{.exposure = 0.5F};
  capture.views2D.clear();
  assert(renderer.renderFrame(world, targetFrame, 0.016F, error));
  assert(std::ranges::any_of(capture.views2D, [](const View2DConfig &view) {
    return view.debugName == "PostProcess";
  }));
  static_cast<void>(graphics.endFrame());
  assert(renderer.renderFrame(world, postProcessFrame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  targetFrame.updateContent = false;
  capture.views2D.clear();
  capture.views3D.clear();
  assert(renderer.renderFrame(world, targetFrame, 0.016F, error));
  assert(capture.views3D.empty());
  assert(!capture.views2D.empty());
  for (const auto &view : capture.views2D) {
    assert(view.debugName != "PostProcess");
    assert(view.frameBuffer == targetFrame.frameBuffer);
  }
  static_cast<void>(graphics.endFrame());
  targetFrame.postProcess.reset();
  Entity targetQuality;
  targetQuality.id = "target-quality";
  targetQuality.setComponent(Environment3DComponent{.msaaSamples = 0});
  world.entities.push_back(std::move(targetQuality));
  targetFrame.updateContent = true;
  assert(renderer.renderFrame(world, targetFrame, .016F, error));
  static_cast<void>(graphics.endFrame());
  world.entities.back().component<Environment3DComponent>()->msaaSamples = 8;
  assert(renderer.renderFrame(world, targetFrame, .016F, error));
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
  world.entities.eraseIf(
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
    peer.setComponent(AnimationPlayer3DComponent{.clipName = "Walk_Loop",
                                                 .time = float(i) * 0.01F});
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
    return entry.name == "Renderer3D.animation_batch_meshes" &&
           entry.gauge == 16;
  }));
  assert(std::any_of(crowd.begin(), crowd.end(), [](const auto &entry) {
    return entry.name == "Renderer3D.animation_batch_vertices" &&
           entry.gauge > 0 && entry.gauge <= 262144;
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
  assert(std::any_of(
      loopChanged.begin(), loopChanged.end(), [](const auto &entry) {
        return entry.name == "Renderer3D.animation_rebuild" && entry.calls == 1;
      }));
  auto *badPose = world.entities.back().component<AnimationPlayer3DComponent>();
  badPose->boneSegments["missing_test_bone"] = {
      {0, 0, 0}, {0, 1, 0}, {0, 0, 1}};
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
  invalidShapeWorld.entities.push_back(
      shape("editable", "unfinished_shape", {}));
  assert(!renderer.renderFrame(invalidShapeWorld, frame, .016F, error));
  invalidShapeWorld.entities.front().component<MeshRendererComponent>()->shape =
      "sphere";
  error.clear();
  assert(renderer.renderFrame(invalidShapeWorld, frame, .016F, error));
  static_cast<void>(graphics.endFrame());

  auto lightingFrame = frame;
  for (int lightingCase = 0; lightingCase < 4; ++lightingCase) {
    lightingFrame.lightingOverride = SceneLighting3D{};
    if (lightingCase == 1) {
      lightingFrame.lightingOverride->lights.push_back({.range = 10});
    } else if (lightingCase == 2) {
      lightingFrame.lightingOverride->lights.push_back(
          {.kind = SceneLightKind3D::Spot,
           .range = 10,
           .outerConeCos = .5F,
           .innerConeCos = .8F});
    }
    RuntimeProfiler::beginFrame();
    assert(renderer.renderFrame(world, lightingFrame, 0.016F, error));
    static_cast<void>(graphics.endFrame());
    const auto entries = RuntimeProfiler::frameEntries();
    const double expected = lightingCase == 0 || lightingCase == 3 ? 1.0 : 0.0;
    assert(std::any_of(entries.begin(), entries.end(),
                       [expected](const auto &entry) {
                         return entry.name == "Renderer3D.directional_shader" &&
                                entry.gauge == expected;
                       }));
  }
  RuntimeProfiler::setEnabled(false);

  World textureWorld;
  auto probe = shape("texture-probe", "cube", {});
  probe.component<MeshRendererComponent>()->texture =
      "asset://textures/override";
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
  assert(capture.textures.size() == 1 &&
         capture.textures.front() != explicitTexture);
  // Static models go through instancing; animated ones use buffered submission.
  // Both must honor the same explicit texture as a primitive, not
  // white/embedded.
  mesh->model = "asset://models/animated";
  mesh->texture = "asset://textures/override";
  for (bool animated : {false, true}) {
    if (animated)
      textureWorld.entities.front().setComponent(
          AnimationPlayer3DComponent{.clipName = "Walk_Loop", .time = .2F});
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
  mesh = textureWorld.entities.front().component<MeshRendererComponent>();
  mesh->model.clear();
  SurfaceRelief3DComponent relief{.heightMap = "asset://textures/override"};
  textureWorld.entities.front().setComponent(relief);
  capture.textures.clear();
  assert(renderer.renderFrame(textureWorld, frame, .016F, error));
  static_cast<void>(graphics.endFrame());
  assert(capture.textures.size() == 1 &&
         capture.textures.front() == explicitTexture);
  ReliefMeshCache3D cache(*resources);
  cache.loadAssets(registry);
  const auto *generated = cache.get(relief, {1, 1, .115F}, error);
  assert(generated && generated->indexCount() > 36 && cache.size() == 1);
  assert(cache.get(relief, {2, 3, .115F}, error) == generated &&
         cache.size() == 1);
  auto regionRelief = relief;
  regionRelief.tiles = {4, 3};
  regionRelief.atlasGrid = {8, 14};
  const auto *regionMesh = cache.get(regionRelief, {4, 3, .115F}, error);
  assert(regionMesh &&
         regionMesh->indexCount() == 12 * generated->indexCount());
  assert(cache.get(regionRelief, {8, 6, .115F}, error) == regionMesh);
  auto flatRegion = regionRelief;
  flatRegion.heightMap.clear();
  flatRegion.depth = 0;
  assert(cache.get(flatRegion, {4, 3, .115F}, error)->indexCount() == 12 * 36);
  assert(!cache.get(relief, {1, 1, .001F}, error));
  cache.clear();
  assert(cache.size() == 0);
  cache.loadAssets(registry);
  auto tile = relief;
  tile.uvScale = {1.F / 16, 1.F / 16};
  for (int i = 0; i < 300; ++i) {
    tile.uvOffset = {float(i % 16) / 16, float(i / 16) / 16};
    assert(cache.get(tile, {1, 1, .115F}, error));
  }
  assert(cache.get(relief, {1, 1, .115F}, error));
  assert(cache.size() > 256); // An active view can exceed the retention budget.
  cache.beginFrame();
  cache.beginFrame();
  relief.uvOffset.y = .731F;
  assert(cache.get(relief, {1, 1, .115F}, error) && cache.size() == 256);
  cache.clear();

  {
    auto project =
        loadProject(std::filesystem::path(DEMI_SOURCE_DIR) /
                        "examples/destruction_3d_lab/demi.project.json",
                    error);
    assert(project);
    auto &debrisWorld = project->world;
    FractureDebris3DComponent effect;
    effect.count = 4;
    effect.lifetime = 2;
    effect.fadeDuration = 1;
    effect.speed = 0;
    effect.gravity = {};
    findEntity(debrisWorld, "arch")->setComponent(effect);
    auto &physics = ensurePhysicsWorld3D(debrisWorld);
    physics.step(debrisWorld, 1.F / 60);
    std::size_t affected = 0;
    assert(debrisWorld.destruction3D->impact(debrisWorld, physics,
                                             {.position = {-1.5F, 1.5F, .49F},
                                              .radius = .2F,
                                              .energy = 1,
                                              .entity = "arch"},
                                             affected, error));
    assert(debrisWorld.destruction3D->cosmeticFragments().size() == 4);
    debrisWorld.destruction3D->updateCosmetics(debrisWorld, 1.5F);
    capture.bufferedStates.clear();
    capture.bufferedAlpha.clear();
    assert(renderer.renderFrame(debrisWorld, frame, .016F, error));
    assert(capture.views3D.back().sequential);
    assert(capture.bufferedStates.size() >= 4);
    for (std::size_t i = capture.bufferedStates.size() - 4;
         i < capture.bufferedStates.size(); ++i) {
      assert(capture.bufferedStates[i].blend == BlendMode::Alpha &&
             !capture.bufferedStates[i].writeDepth);
      assert(std::abs(capture.bufferedAlpha[i] - .5F) < 1e-5F);
    }
    (void)graphics.endFrame();
    debrisWorld.destruction3D->updateCosmetics(debrisWorld, 1);
    assert(renderer.renderFrame(debrisWorld, frame, .016F, error));
    assert(!capture.views3D.back().sequential);
    (void)graphics.endFrame();
  }
  for (int kind = 0; kind < 3; ++kind) {
    World fading;
    Entity fragment;
    fragment.id = "actual_fragment";
    fragment.setComponent(Transform3DComponent{});
    MeshRendererComponent mesh;
    mesh.color = {1, 1, 1, 1};
    if (kind == 1)
      mesh.model = "asset://models/lod_high";
    fragment.setComponent(mesh);
    if (kind == 2) {
      SurfaceRelief3DComponent surface;
      surface.tiles = {2, 2};
      fragment.setComponent(surface);
    }
    fragment.setComponent(FragmentOpacity3D{.value = .5F});
    fading.entities.push_back(fragment);
    capture.bufferedStates.clear();
    capture.bufferedAlpha.clear();
    assert(renderer.renderFrame(fading, frame, .016F, error));
    assert(capture.views3D.back().sequential);
    assert(!capture.bufferedStates.empty());
    assert(capture.bufferedStates.back().blend == BlendMode::Alpha &&
           !capture.bufferedStates.back().writeDepth);
    assert(std::abs(capture.bufferedAlpha.back() - .5F) < 1e-5F);
    (void)graphics.endFrame();
  }
  {
    World boundsWorld;
    auto owner = shape("model-bounds", "", {});
    auto *mesh = owner.component<MeshRendererComponent>();
    mesh->model = "asset://bounds/high";
    mesh->mediumLodModel = "asset://bounds/medium";
    MeshInstances3DComponent instances;
    instances.setTransform("visible", {.position = {0, 0, 5}});
    instances.setTransform("outside", {.position = {100, 0, 5}});
    owner.setComponent(instances);
    boundsWorld.entities.push_back(owner);
    BgfxCameraFrame3D boundsFrame;
    boundsFrame.camera.perspective = false;
    boundsFrame.camera.orthographicSize = 1.F;
    boundsFrame.viewportWidth = 100;
    boundsFrame.viewportHeight = 100;
    int modelLookups = 0;
    const ModelBoundsResolver3D resolver = [&](std::string_view id) {
      ++modelLookups;
      const float extent = id == "asset://bounds/medium" ? 2.F : .5F;
      return std::optional<MeshBounds3D>(MeshBounds3D{
          {-extent, -extent, -extent}, {extent, extent, extent}});
    };
    const auto visible = extractVisibleMeshes3D(
        boundsWorld, boundsFrame, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, resolver);
    assert(visible.considered == 2 && visible.culled == 1);
    assert(visible.meshes.size() == 1 && modelLookups == 2);

    // A reduced model can have larger bounds than its source. Their union
    // preserves visibility near a frustum edge regardless of selected tier.
    boundsWorld.entities.front().component<MeshInstances3DComponent>()
        ->setTransform("visible", {.position = {3.F, 0, 5}});
    assert(extractVisibleMeshes3D(
        boundsWorld, boundsFrame, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, resolver).meshes.size() == 1);

    // Ordinary primitives use canonical unit bounds without authored vertices.
    auto primitive = shape("primitive-bounds", "cube", {100, 0, 5});
    World primitiveWorld;
    primitiveWorld.entities.push_back(primitive);
    assert(extractVisibleMeshes3D(primitiveWorld, boundsFrame).meshes.empty());

    const auto windPath = std::filesystem::temp_directory_path() /
        ("demi-wind-bounds-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
    struct RemoveWindFixture {
      std::filesystem::path path;
      ~RemoveWindFixture() { std::filesystem::remove(path); }
    } cleanup{windPath};
    {
      std::ofstream output(windPath);
      output << R"({"format_version":1,"parameters":{"wind_strength":5}})";
    }
    demi::AssetRegistry windRegistry;
    windRegistry.assets.push_back({.id = "asset://bounds/wind",
                                   .type = "Material", .sourcePath = windPath});
    MaterialLibrary windMaterials(*resources);
    std::vector<std::string> windDiagnostics;
    assert(windMaterials.load(windRegistry, windDiagnostics));
    primitiveWorld.entities.front().updateComponent<Transform3DComponent>([&](auto &transform) { transform.position.x = 3.F; });
    primitiveWorld.entities.front().component<MeshRendererComponent>()->material = "asset://bounds/wind";
    assert(extractVisibleMeshes3D(primitiveWorld, boundsFrame).meshes.empty());
    assert(extractVisibleMeshes3D(
        primitiveWorld, boundsFrame, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, &windMaterials).meshes.size() == 1);

    MeshUploadCapture windResources(*resources);
    TextureCapture windCapture(*commands);
    windCapture.meshWindUniform = &windResources.meshWindUniform;
    BgfxRenderer3D windRenderer(windResources, windCapture);
    assert(windRenderer.initialize(error));
    assert(windRenderer.loadAssets(windRegistry, windDiagnostics));
    World windWorld;
    auto foliage = shape("wind-phase", "cube", {});
    foliage.component<MeshRendererComponent>()->material = "asset://bounds/wind";
    windWorld.entities.push_back(foliage);
    Entity sun;
    sun.id = "wind-sun";
    sun.setComponent(DirectionalLightComponent{.castsShadows = true});
    sun.setComponent(Environment3DComponent{.shadowCascades = 1});
    windWorld.entities.push_back(sun);
    assert(windRenderer.renderFrame(windWorld, frame, .125F, error));
    assert(windCapture.windTimeByView.contains(frame.viewId));
    assert(windCapture.windTimeByView.contains(frame.viewId + 1));
    assert(windCapture.windTimeByView.at(frame.viewId) == .125F);
    assert(windCapture.windTimeByView.at(frame.viewId + 1) == .125F);
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
    instances.setTransform("left", {.position = {-101.0F, 0.0F, 0.0F}});
    instances.setTransform("right", {.position = {-99.0F, 0.0F, 0.0F}});
    instances.setTransform("outside", {.position = {100.0F, 0.0F, 0.0F}});
    owner.setComponent(instances);
    instanceWorld.entities.push_back(owner);

    const auto visible = extractVisibleMeshes3D(instanceWorld, frame);
    assert(visible.considered == 3 && visible.culled == 1 &&
           visible.meshes.size() == 2);
    capture.transformsByView.clear();
    capture.instanceDrawCalls = 0;
    assert(renderer.renderFrame(instanceWorld, frame, 0.016F, error));
    assert(capture.transformsByView.at(frame.viewId).size() == 2);
    assert(capture.instanceDrawCalls == 1);
    (void)graphics.endFrame();

    auto *inlineMesh = instanceWorld.entities.front()
                           .component<MeshRendererComponent>();
    inlineMesh->vertices = {{-0.5F, 0, 0}, {0.5F, 0, 0}, {0, 1, 0}};
    capture.instanceDrawCalls = 0;
    assert(renderer.renderFrame(instanceWorld, frame, 0.016F, error));
    assert(capture.instanceDrawCalls == 1);
    (void)graphics.endFrame();

    instanceWorld.entities.front().setComponent(AnimationPlayer3DComponent{});
    assert(!renderer.renderFrame(instanceWorld, frame, 0.016F, error));
    assert(error.find("shared static mesh") != std::string::npos);
    instanceWorld.entities.front().removeComponent<AnimationPlayer3DComponent>();
    (void)graphics.endFrame();

    instanceWorld.entities.front()
        .component<MeshInstances3DComponent>()
        ->clearTransforms();
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
    auto source = std::make_shared<HeightField>();
    source->size = {2, 2};
    source->cellsX = 8;
    source->cellsZ = 8;
    source->biomeIds = {"grass"};
    source->biomeColors = {{0.3F, 0.7F, 0.9F, 1}};
    source->heights.resize(81);
    source->normals.resize(81);
    source->biomeIndices.resize(81);
    for (int z = 0; z <= 8; ++z) {
      for (int x = 0; x <= 8; ++x) {
        const auto sample = source->index(x, z);
        source->heights.set(sample, float((x + z) % 2) * 0.5F);
        source->normals.set(sample, Vec3{0, 1, 0});
        source->biomeIndices.set(sample, 0);
      }
    }
    const TerrainChunk rectangle{0, 0, 8, 8};
    source->chunks.push_back(rectangle);
    const auto triangles =
        terrain_detail::buildChunkTriangles(*source, rectangle);
    full->vertices = triangles.at(0).vertices;
    full->normals = triangles.at(0).normals;
    full->uvs = triangles.at(0).uvs;
    full->markGeometryChanged();
    const auto fullRevision = full->revision;
    const auto chunk = prepareTerrainRenderLod(source, rectangle);
    const auto fullTriangles = chunk->triangleCounts[0];
    const auto reducedTriangles = chunk->triangleCounts[3];
    surface.setComponent(TerrainRenderLod{chunk, "grass", "terrain-owner"});
    terrainWorld.entities.push_back(std::move(surface));
    // Native TerrainWorld publication registers owned surfaces. Keep this
    // focused CPU-LOD fixture discoverable by presentation/demand replay too.
    terrainWorld.terrainOwners.push_back(
        {.id = "terrain-owner", .surfaces = {"terrain-surface"}});
    auto nearCamera = frame;
    nearCamera.cameraId = "near-camera";
    nearCamera.camera.renderHud = false;
    auto farCamera = nearCamera;
    farCamera.cameraId = "far-camera";
    farCamera.position = {0, 0, 500};
    farCamera.forward = {0, 0, -1};
    farCamera.camera.farClip = 1000;
    assert(terrainRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
    assert(terrainRenderer.terrainStatistics().selectedSurfaces[0] == 1);
    assert(terrainRenderer.terrainStatistics().triangles == fullTriangles);
    assert(terrainRenderer.terrainStatistics().uploads == 1);
    assert(terrainRenderer.terrainPreparationPending());
    static_cast<void>(graphics.endFrame());
    const auto awaitReduced = [&](BgfxRenderer3D &target) {
      const auto deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(2);
      do {
        assert(target.renderFrame(terrainWorld, farCamera, 0.016F, error));
        static_cast<void>(graphics.endFrame());
        if (target.terrainStatistics().selectedSurfaces[3] == 1) {
          assert(target.terrainStatistics().triangles == reducedTriangles);
          assert(!target.terrainPreparationPending());
          return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } while (std::chrono::steady_clock::now() < deadline);
      assert(false &&
             "Terrain preparation did not complete before its bounded timeout");
    };
    awaitReduced(terrainRenderer);
    assert(terrainRenderer.terrainStatistics().cpuResidentBytes > 0);
    assert(terrainRenderer.terrainStatistics().cpuReadyLevels == 1);
    {
      auto scheduled = farCamera;
      scheduled.cameraId = "scheduled-terrain-camera";
      assert(
          terrainRenderer.renderFrame(terrainWorld, scheduled, 0.016F, error));
      static_cast<void>(graphics.endFrame());
      scheduled.updateContent = false;
      for (int skipped = 0; skipped < 10; ++skipped) {
        assert(terrainRenderer.renderFrame(terrainWorld, scheduled, 0.016F,
                                           error));
        assert(terrainRenderer.terrainStatistics().cpuReadyLevels == 1);
        static_cast<void>(graphics.endFrame());
      }
      scheduled.updateContent = true;
      assert(
          terrainRenderer.renderFrame(terrainWorld, scheduled, 0.016F, error));
      assert(terrainRenderer.terrainStatistics().selectedSurfaces[3] == 1);
      static_cast<void>(graphics.endFrame());

      // Reusing the same world address/camera ID cannot replay a replaced
      // geometry source on intervening scheduled-target frames.
      auto replaced = std::make_shared<PreparedTerrainChunkLod>(*chunk);
      replaced->geometryIdentity =
          std::make_shared<TerrainLodGeometryIdentity>();
      terrainWorld.entities[1].setComponent(
          TerrainRenderLod{replaced, "grass", "terrain-owner"});
      scheduled.updateContent = false;
      assert(
          terrainRenderer.renderFrame(terrainWorld, scheduled, 0.016F, error));
      assert(terrainRenderer.terrainStatistics().cpuReadyLevels == 0);
      assert(!terrainRenderer.terrainPreparationPending());
      static_cast<void>(graphics.endFrame());
      terrainWorld.entities[1].setComponent(
          TerrainRenderLod{chunk, "grass", "terrain-owner"});
      // Removing the scheduled camera also retires its weak demand state.
      assert(
          terrainRenderer.renderFrame(terrainWorld, nearCamera, 0.016F, error));
      assert(terrainRenderer.terrainStatistics().cpuReadyLevels == 0);
      static_cast<void>(graphics.endFrame());
      terrainRenderer.tickInactiveTerrain();
      awaitReduced(terrainRenderer);
    }
    {
      JobSystem shadowPreparationJobs(1);
      TerrainLodResidency shadowResidency(shadowPreparationJobs);
      shadowResidency.beginPresentation();
      assert(!shadowResidency.request(chunk, 3));
      shadowResidency.endPresentation();
      // A worker queue fence is test synchronization only; renderer request
      // paths never wait for terrain preparation.
      shadowPreparationJobs.submit([] {}).wait();
      shadowResidency.beginPresentation();
      auto shadowCamera = farCamera;
      shadowCamera.camera.perspective = false;
      shadowCamera.camera.orthographicSize = 2;
      shadowCamera.position = {0, 0, 5};
      const auto shadowDraws =
          extractVisibleMeshes3D(terrainWorld, shadowCamera, nullptr, nullptr,
                                 &farCamera, &shadowResidency);
      assert(!shadowDraws.meshes.empty());
      for (const auto &draw : shadowDraws.meshes) {
        assert(draw.terrainLevel == 3);
        assert(draw.terrainPreparedLevel);
      }
      shadowResidency.endPresentation();
    }
    // Keep far-camera demand while testing the independent near Full decision.
    terrainRenderer.beginPresentation();
    assert(
        terrainRenderer.renderFrame(terrainWorld, nearCamera, 0.016F, error));
    assert(terrainRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
    terrainRenderer.endPresentation(true);
    assert(terrainRenderer.terrainStatistics().selectedSurfaces[0] == 1);
    assert(terrainRenderer.terrainStatistics().selectedSurfaces[3] == 1);
    static_cast<void>(graphics.endFrame());
    assert(
        terrainWorld.entities[1].component<MeshRendererComponent>()->revision ==
        fullRevision);
    assert(terrainWorld.entities[1]
               .component<MeshRendererComponent>()
               ->vertices.size() == fullTriangles * 3);

    // Multiple cameras retain the union of Full and reduced GPU demand.
    for (int presentation = 0; presentation < 10; ++presentation) {
      terrainRenderer.beginPresentation();
      assert(
          terrainRenderer.renderFrame(terrainWorld, nearCamera, 0.016F, error));
      assert(
          terrainRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
      terrainRenderer.endPresentation(true);
      assert(terrainRenderer.terrainStatistics().residentMeshes == 2);
      assert(terrainRenderer.terrainStatistics().uploads == 0);
      static_cast<void>(graphics.endFrame());
    }
    // The Full CPU mesh survives when its unused GPU variant is evicted.
    for (int presentation = 0; presentation < 10; ++presentation) {
      assert(
          terrainRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
      static_cast<void>(graphics.endFrame());
    }
    assert(terrainRenderer.terrainStatistics().residentMeshes == 1);
    assert(
        terrainRenderer.renderFrame(terrainWorld, nearCamera, 0.016F, error));
    assert(terrainRenderer.terrainStatistics().uploads == 1);
    static_cast<void>(graphics.endFrame());

    // An upload failure after CPU preparation is ready must preserve Full.
    BgfxRenderer3D fallbackRenderer(uploads, *commands);
    assert(fallbackRenderer.initialize(error));
    assert(
        fallbackRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
    static_cast<void>(graphics.endFrame());
    const auto failureDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    do {
      uploads.failNextMeshUpload = true;
      assert(
          fallbackRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
      static_cast<void>(graphics.endFrame());
      if (fallbackRenderer.terrainStatistics().cpuReadyLevels != 0 &&
          !uploads.failNextMeshUpload) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < failureDeadline);
    assert(fallbackRenderer.terrainStatistics().cpuReadyLevels == 1);
    assert(!uploads.failNextMeshUpload);
    assert(fallbackRenderer.terrainStatistics().selectedSurfaces[0] == 1);
    assert(fallbackRenderer.terrainStatistics().triangles == fullTriangles);
    assert(error.empty());
    awaitReduced(fallbackRenderer);

    awaitReduced(terrainRenderer);
    const auto residentBeforeSharedPlacement =
        terrainRenderer.terrainStatistics().residentMeshes;
    terrainWorld.entities.front()
        .component<Terrain3DComponent>()
        ->lodErrorPixels = 0;
    Entity secondOwner;
    secondOwner.id = "second-terrain-owner";
    secondOwner.setComponent(Transform3DComponent{.position = {0, 0, -100}});
    secondOwner.setComponent(Terrain3DComponent{});
    auto secondSurface = terrainWorld.entities[1];
    secondSurface.id = "second-terrain-surface";
    secondSurface.setComponent(Transform3DComponent{.parent = secondOwner.id});
    secondSurface.setComponent(
        TerrainRenderLod{chunk, "grass", secondOwner.id});
    terrainWorld.entities.push_back(std::move(secondOwner));
    terrainWorld.entities.push_back(std::move(secondSurface));
    assert(terrainRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
    assert(terrainRenderer.terrainStatistics().selectedSurfaces[0] == 1);
    assert(terrainRenderer.terrainStatistics().selectedSurfaces[3] == 1);
    // Shared immutable reduced geometry uses the existing GPU variant even
    // though the placement, transform, quality and stable surface ID differ.
    assert(terrainRenderer.terrainStatistics().uploads == 0);
    assert(terrainRenderer.terrainStatistics().residentMeshes ==
           residentBeforeSharedPlacement);
    static_cast<void>(graphics.endFrame());
    // A previous CPU eviction/rebuild may leave an older GPU variant in its
    // short grace window. Sharing adds no entry, and all unused variants must
    // retire while the two current placement demands remain active.
    for (int presentation = 0; presentation < 10; ++presentation) {
      assert(
          terrainRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
      assert(terrainRenderer.terrainStatistics().uploads == 0);
      static_cast<void>(graphics.endFrame());
    }
    assert(terrainRenderer.terrainStatistics().residentMeshes == 2);

    // Worker failures are optional-render diagnostics, not frame failures.
    // Report the actual reason once, then stop scheduling/retrying that source.
    auto invalidDescriptor = std::make_shared<PreparedTerrainChunkLod>(*chunk);
    invalidDescriptor->field.reset();
    invalidDescriptor->geometryIdentity =
        std::make_shared<TerrainLodGeometryIdentity>();
    terrainWorld.entities.front()
        .component<Terrain3DComponent>()
        ->lodErrorPixels = 2;
    terrainWorld.entities[1].setComponent(
        TerrainRenderLod{invalidDescriptor, "grass", "terrain-owner"});
    const auto diagnosticDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    std::string terrainDiagnostic;
    do {
      assert(
          terrainRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
      static_cast<void>(graphics.endFrame());
      terrainDiagnostic = terrainRenderer.takeTerrainDiagnostic();
      if (!terrainDiagnostic.empty()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < diagnosticDeadline);
    assert(terrainDiagnostic.find("Full terrain remains visible") !=
           std::string::npos);
    assert(terrainDiagnostic.find("field") != std::string::npos ||
           terrainDiagnostic.find("source") != std::string::npos ||
           terrainDiagnostic.find("descriptor") != std::string::npos);
    assert(terrainRenderer.terrainStatistics().selectedSurfaces[0] == 1);
    assert(!terrainRenderer.terrainPreparationPending());
    assert(terrainRenderer.takeTerrainDiagnostic().empty());
    assert(terrainRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
    assert(terrainRenderer.takeTerrainDiagnostic().empty());
    static_cast<void>(graphics.endFrame());
    terrainWorld.entities[1].setComponent(
        TerrainRenderLod{chunk, "grass", "terrain-owner"});
    terrainWorld.entities.front()
        .component<Terrain3DComponent>()
        ->lodErrorPixels = 0;

    // Hidden-view maintenance releases geometry immediately. Its externally
    // owned image target remains valid, so no redraw is needed for retirement.
    const auto retainedImage = resources->createRenderTarget(
        {.width = 320,
         .height = 180,
         .debugName = "Retained hidden terrain image"},
        error);
    assert(retainedImage.frameBuffer && retainedImage.color);
    terrainRenderer.tickInactiveTerrain();
    assert(terrainRenderer.terrainStatistics().cpuResidentBytes == 0);
    assert(terrainRenderer.terrainStatistics().cpuReadyLevels == 0);
    assert(terrainRenderer.terrainStatistics().residentMeshes == 0);
    assert(
        terrainWorld.entities[1].component<MeshRendererComponent>()->revision ==
        fullRevision);
    assert(resources->destroy(retainedImage.frameBuffer));
    if (retainedImage.depth) {
      assert(resources->destroy(retainedImage.depth));
    }
    assert(resources->destroy(retainedImage.color));

    // A zero preparation budget is an explicit Full-only fallback and cannot
    // keep an idle editor scheduling frames forever.
    Entity environment;
    environment.id = "disabled-terrain-preparation";
    Environment3DComponent settings;
    settings.terrainLodCacheBytes = 0;
    settings.terrainLodPrepareJobs = 0;
    environment.setComponent(settings);
    terrainWorld.entities.push_back(std::move(environment));
    terrainWorld.entities.front()
        .component<Terrain3DComponent>()
        ->lodErrorPixels = 2;
    for (int presentation = 0; presentation < 3; ++presentation) {
      assert(
          terrainRenderer.renderFrame(terrainWorld, farCamera, 0.016F, error));
      assert(terrainRenderer.terrainStatistics().selectedSurfaces[0] == 2);
      assert(!terrainRenderer.terrainPreparationPending());
      assert(terrainRenderer.terrainStatistics().cpuResidentBytes == 0);
      static_cast<void>(graphics.endFrame());
    }
  }
  {
    MeshUploadCapture uploads(*resources);
    TextureCapture terrainCommands(*commands);
    BgfxRenderer3D gpuTerrain(uploads, terrainCommands);
    assert(gpuTerrain.initialize(error));
    const auto fixturePath =
        std::filesystem::temp_directory_path() /
        ("demi-mapped-residency-" +
         std::to_string(
             std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(fixturePath);
    struct FixtureCleanup {
      std::filesystem::path path;
      ~FixtureCleanup() { std::filesystem::remove_all(path); }
    } cleanup{fixturePath};
    const auto materialPath = fixturePath / "terrain.material.json";
    {
      std::ofstream output(materialPath);
      output << R"({
        "format_version": 1,
        "shader": "builtin://lit",
        "textures": {
          "albedo": "asset://gpu/shared-color",
          "normal": "asset://gpu/terrain-normal"
        },
        "parameters": {"roughness": 0.4}
      })";
    }
    demi::AssetRegistry mappedRegistry;
    mappedRegistry.assets.push_back({.id = "asset://gpu/terrain-material",
                                     .type = "Material",
                                     .sourcePath = materialPath});
    for (const std::string id :
         {"asset://gpu/shared-color", "asset://gpu/terrain-normal"}) {
      demi::AssetManifest image{.id = id, .type = "Texture2D"};
      image.sourcePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                         "examples/minimal_3d/assets/textures/checker.png";
      image.textureSettings.wrap = "repeat";
      image.textureSettings.mipmaps = true;
      mappedRegistry.assets.push_back(std::move(image));
    }
    std::ranges::sort(mappedRegistry.assets, {}, &demi::AssetManifest::id);
    assert(gpuTerrain.loadAssets(mappedRegistry, diagnostics));
    TerrainRecipe recipe;
    recipe.size = {16, 16};
    recipe.cellsX = 16;
    recipe.cellsZ = 16;
    recipe.chunkCells = 8;
    recipe.landforms.at("default").baseHeight = 0;
    recipe.landforms.at("default").heightVariation = 0;
    for (auto &[id, biome] : recipe.biomes) {
      biome.material = "asset://gpu/terrain-material";
    }
    const auto generated = TerrainGenerator::generate(recipe);
    assert(generated);
    const auto source = std::make_shared<const HeightField>(*generated);
    World terrain;
    Entity owner;
    owner.id = "gpu-terrain-owner";
    owner.setComponent(Transform3DComponent{});
    Terrain3DComponent component;
    component.asset = "asset://test/gpu-terrain";
    component.lodErrorPixels = 0;
    owner.setComponent(component);
    terrain.entities.push_back(std::move(owner));
    const TerrainFieldResolver resolver = [&](std::string_view,
                                              nlohmann::json &outRecipe) {
      outRecipe = recipe.toJson();
      return source;
    };
    assert(materializeTerrains(terrain, error, nullptr, {}, resolver));
    auto camera = frame;
    camera.cameraId = "gpu-full-detail";
    camera.camera.perspective = false;
    camera.camera.orthographicSize = 20;
    camera.position = {8, 100, 8};
    camera.forward = {0, -1, 0};
    camera.up = {0, 0, -1};
    camera.camera.renderHud = false;
    auto ordinary = shape("shared-material-mesh", "cube", {8, 1, 8});
    ordinary.component<MeshRendererComponent>()->texture =
        "asset://gpu/shared-color";
    terrain.entities.push_back(std::move(ordinary));
    Entity compatibility;
    compatibility.id = "warm-up-compatibility";
    Environment3DComponent warmupSettings;
    warmupSettings.terrainGpuCacheBytes = 0;
    compatibility.setComponent(warmupSettings);
    terrain.entities.push_back(std::move(compatibility));
    assert(gpuTerrain.renderFrame(terrain, camera, 0.016F, error));
    static_cast<void>(graphics.endFrame());
    const auto uploadedHandle = [&](std::string_view name) {
      for (std::size_t index = 0; index < uploads.textureNames.size();
           ++index) {
        if (uploads.textureNames[index] == name) {
          return uploads.textureHandles[index];
        }
      }
      return TextureHandle{};
    };
    const auto fallbackNormal = uploadedHandle("asset://gpu/terrain-normal");
    const auto sharedColor = uploadedHandle("asset://gpu/shared-color");
    assert(fallbackNormal && sharedColor);
    terrain.entities.pop_back();
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    do {
      assert(gpuTerrain.renderFrame(terrain, camera, 0.016F, error));
      static_cast<void>(graphics.endFrame());
      if (gpuTerrain.terrainStatistics().gpuPatchInstances == 4) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    assert(gpuTerrain.terrainStatistics().gpuPatchInstances == 4);
    assert(gpuTerrain.terrainStatistics().gpuPatchDraws == 1);
    assert(gpuTerrain.terrainStatistics().gpuSelectedLevels[0] == 4);
    assert(gpuTerrain.terrainStatistics().selectedSurfaces[0] == 0);
    assert(gpuTerrain.terrainStatistics().triangles == 16 * 16 * 2);
    for (unsigned graceFrame = 0; graceFrame < 2; ++graceFrame) {
      assert(gpuTerrain.renderFrame(terrain, camera, 0.016F, error));
      static_cast<void>(graphics.endFrame());
    }
    assert(std::find(uploads.retiredTextures.begin(),
                     uploads.retiredTextures.end(),
                     fallbackNormal) != uploads.retiredTextures.end());
    assert(std::find(uploads.retiredTextures.begin(),
                     uploads.retiredTextures.end(),
                     sharedColor) == uploads.retiredTextures.end());
    const auto fullGpuUploads = uploads.meshUploads.size();
    assert(gpuTerrain.renderFrame(terrain, camera, 0.016F, error));
    assert(uploads.meshUploads.size() == fullGpuUploads);
    assert(gpuTerrain.statistics().batches <= 3);
    static_cast<void>(graphics.endFrame());

    // The editor's active view intent survives target preparation or a skipped
    // draw. Both terrain bundles and an ordinary material share this lifecycle.
    demi::editor::EditorTerrainViewDemand viewDemand;
    viewDemand.request3D();
    assert(viewDemand.finishFrame());
    const auto textureUploads = uploads.textureHandles.size();
    const auto fieldHeight = uploadedHandle("Terrain GPU heights");
    assert(fieldHeight);
    for (unsigned skipped = 0; skipped < 3; ++skipped) {
      viewDemand.observeVisibility(true);
      if (!viewDemand.finishFrame()) {
        gpuTerrain.tickInactiveTerrain();
      }
      viewDemand.request3D();
      assert(gpuTerrain.renderFrame(terrain, camera, .016F, error));
      assert(viewDemand.finishFrame());
      static_cast<void>(graphics.endFrame());
      assert(gpuTerrain.terrainStatistics().gpuPatchInstances == 4);
      assert(uploads.textureHandles.size() == textureUploads);
      assert(uploads.meshUploads.size() == fullGpuUploads);
      assert(std::ranges::find(uploads.retiredTextures, fieldHeight) ==
             uploads.retiredTextures.end());
      assert(std::ranges::find(uploads.retiredTextures, sharedColor) ==
             uploads.retiredTextures.end());
    }
    // Actual inactivity still releases the terrain budget. Ordinary material
    // assets remain owned by publication, and resume uses the normal warmup.
    assert(!viewDemand.finishFrame());
    gpuTerrain.tickInactiveTerrain();
    assert(std::ranges::find(uploads.retiredTextures, fieldHeight) !=
           uploads.retiredTextures.end());
    assert(std::ranges::find(uploads.retiredTextures, sharedColor) ==
           uploads.retiredTextures.end());
    const auto resumeDeadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    do {
      viewDemand.request3D();
      assert(gpuTerrain.renderFrame(terrain, camera, .016F, error));
      assert(viewDemand.finishFrame());
      static_cast<void>(graphics.endFrame());
      if (gpuTerrain.terrainStatistics().gpuPatchInstances == 4) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < resumeDeadline);
    assert(gpuTerrain.terrainStatistics().gpuPatchInstances == 4);
    assert(uploads.textureHandles.size() > textureUploads);
    assert(std::ranges::find(uploads.retiredTextures, sharedColor) ==
           uploads.retiredTextures.end());

    // The deliberate compatibility switch returns to complete native Full
    // geometry without changing the field, picking/collision or authored data.
    Entity environment;
    environment.id = "cpu-terrain-compatibility";
    Environment3DComponent settings;
    settings.terrainGpuCacheBytes = 0;
    environment.setComponent(settings);
    terrain.entities.push_back(std::move(environment));
    assert(gpuTerrain.renderFrame(terrain, camera, 0.016F, error));
    assert(gpuTerrain.terrainStatistics().gpuPatchInstances == 0);
    assert(gpuTerrain.terrainStatistics().selectedSurfaces[0] == 4);
    assert(gpuTerrain.terrainStatistics().triangles == 16 * 16 * 2);
    static_cast<void>(graphics.endFrame());
  }
  renderer.shutdown();
  renderer.shutdown();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}
