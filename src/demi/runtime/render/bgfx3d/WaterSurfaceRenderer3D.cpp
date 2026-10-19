#include "demi/runtime/render/bgfx3d/WaterSurfaceRenderer3D.h"

#include "demi/runtime/render/backend/ImageColorSpace.h"
#include "demi/runtime/render/backend/RenderCommands.h"
#include "demi/runtime/render/bgfx3d/MeshTransform3D.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainWaterFlow.h"
#include "demi/runtime/terrain/TerrainWaterMesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <vector>

namespace demi::runtime::render {
namespace {

struct WaterVertex {
  Vec3 position;
  Vec3 normal;
  std::array<float, 3> water;
};

Vec3 normalized(Vec3 value, Vec3 fallback) {
  const float length = std::hypot(value.x, value.y, value.z);
  if (!(length > 0)) {
    return fallback;
  }
  return {value.x / length, value.y / length, value.z / length};
}

Vec3 cross(Vec3 first, Vec3 second) {
  return {first.y * second.z - first.z * second.y,
          first.z * second.x - first.x * second.z,
          first.x * second.y - first.y * second.x};
}

std::array<float, 4> linearColor(Color color) {
  return {float(decodeSrgbChannel(color.r)), float(decodeSrgbChannel(color.g)),
          float(decodeSrgbChannel(color.b)), color.a};
}

constexpr std::array uniformNames{
    "u_waterCamera",      "u_waterForward",        "u_waterRight",
    "u_waterUp",          "u_waterCapture",        "u_waterProjection",
    "u_waterLocalCamera", "u_waterShallow",        "u_waterDeep",
    "u_waterOptics",      "u_waterWaves",          "u_waterFoam",
    "u_waterFoamColor",   "u_waterLightDirection", "u_waterLightColor",
    "u_waterAmbient"};

struct WaterGeometry {
  explicit WaterGeometry(GpuResources &owner) : resources(owner) {}
  ~WaterGeometry() {
    if (vertices) {
      resources.destroy(vertices);
    }
    if (indices) {
      resources.destroy(indices);
    }
  }
  GpuResources &resources;
  std::shared_ptr<const HeightField> source;
  std::string bodyId;
  BufferHandle vertices;
  BufferHandle indices;
  std::uint32_t vertexCount = 0;
  std::uint32_t indexCount = 0;
  bool touched = false;
};

} // namespace

struct WaterSurfaceRenderer3D::Impl {
  Impl(GpuResources &gpu, RenderCommands &draws)
      : resources(gpu), commands(draws) {}
  GpuResources &resources;
  RenderCommands &commands;
  ProgramHandle program;
  std::array<SamplerHandle, 3> samplers{};
  std::array<UniformHandle, uniformNames.size()> uniforms{};
  std::map<std::string, std::unique_ptr<WaterGeometry>> geometry;
  WaterSurfaceStatistics3D statistics;

  WaterGeometry *
  prepare(const Entity &entity,
          const terrain_detail::TerrainGeneratedWaterSurface &water,
          std::string &error) {
    auto &entry = geometry[entity.id];
    if (entry && entry->source == water.sourceField &&
        entry->bodyId == water.bodyId) {
      entry->touched = true;
      return entry.get();
    }
    if (!water.sourceField || !water.sourceField->graphArtifacts ||
        !water.sourceField->graphArtifacts->waterResult) {
      error =
          "Water rendering requires retained prepared water data: " + entity.id;
      return nullptr;
    }
    const auto &artifacts = *water.sourceField->graphArtifacts;
    const auto surface =
        std::ranges::find(artifacts.waterResult->surfaces, water.bodyId,
                          &TerrainWaterSurface::id);
    const auto body = std::ranges::find(artifacts.water.bodies, water.bodyId,
                                        &TerrainWaterBodySpec::id);
    if (surface == artifacts.waterResult->surfaces.end() ||
        body == artifacts.water.bodies.end() ||
        surface->vertices.size() > UINT32_MAX ||
        surface->indices.size() > UINT32_MAX) {
      error = "Water rendering cannot resolve prepared body: " + water.bodyId;
      return nullptr;
    }
    std::vector<WaterVertex> vertices;
    vertices.reserve(surface->vertices.size());
    for (std::size_t index = 0; index < surface->vertices.size(); ++index) {
      const auto position = surface->vertices[index];
      const auto flow =
          terrainWaterFlowVelocity(*body, position, surface->normals.at(index));
      vertices.push_back({position,
                          surface->normals.at(index),
                          {surface->depth.at(index), flow.x, flow.z}});
    }
    std::vector<std::uint32_t> indices;
    indices.reserve(surface->indices.size());
    for (const auto index : surface->indices) {
      indices.push_back(static_cast<std::uint32_t>(index));
    }
    auto replacement = std::make_unique<WaterGeometry>(resources);
    replacement->source = water.sourceField;
    replacement->bodyId = water.bodyId;
    replacement->vertices = resources.createBuffer(
        {.kind = BufferKind::Vertex,
         .data = std::as_bytes(std::span(vertices)),
         .vertexLayout = {.attributes = {{.semantic = VertexSemantic::Position,
                                          .components = 3},
                                         {.semantic = VertexSemantic::Normal,
                                          .components = 3},
                                         {.semantic = VertexSemantic::TexCoord1,
                                          .components = 3}}},
         .debugName = "Water surface vertices " + water.bodyId},
        error);
    if (!replacement->vertices) {
      return nullptr;
    }
    replacement->indices = resources.createBuffer(
        {.kind = BufferKind::Index32,
         .data = std::as_bytes(std::span(indices)),
         .debugName = "Water surface triangles " + water.bodyId},
        error);
    if (!replacement->indices) {
      return nullptr;
    }
    replacement->vertexCount = static_cast<std::uint32_t>(vertices.size());
    replacement->indexCount = static_cast<std::uint32_t>(indices.size());
    replacement->touched = true;
    entry = std::move(replacement);
    return entry.get();
  }
};

WaterSurfaceRenderer3D::WaterSurfaceRenderer3D(GpuResources &resources,
                                               RenderCommands &commands)
    : impl_(std::make_unique<Impl>(resources, commands)) {}

WaterSurfaceRenderer3D::~WaterSurfaceRenderer3D() { clear(); }

bool WaterSurfaceRenderer3D::initialize(std::string &error) {
  if (impl_->program) {
    return true;
  }
  impl_->program = impl_->resources.createBuiltinProgram(
      BuiltinProgram::WaterSurface3D, error);
  constexpr std::array samplerNames{"s_waterSceneColor", "s_waterSceneDepth",
                                    "s_waterEnvironment"};
  for (std::size_t index = 0; index < samplerNames.size(); ++index) {
    impl_->samplers[index] =
        impl_->resources.createSampler(samplerNames[index], error);
  }
  for (std::size_t index = 0; index < uniformNames.size(); ++index) {
    impl_->uniforms[index] = impl_->resources.createUniform(
        uniformNames[index], UniformType::Vec4, 1, error);
  }
  const bool valid =
      impl_->program &&
      std::ranges::all_of(impl_->samplers,
                          [](auto handle) { return bool(handle); }) &&
      std::ranges::all_of(impl_->uniforms,
                          [](auto handle) { return bool(handle); });
  if (!valid) {
    clear();
  }
  return valid;
}

void WaterSurfaceRenderer3D::beginPresentation() {
  impl_->statistics = {};
  for (auto &[id, geometry] : impl_->geometry) {
    if (geometry) {
      geometry->touched = false;
    }
  }
}

void WaterSurfaceRenderer3D::endPresentation() {
  std::erase_if(impl_->geometry, [](const auto &entry) {
    return !entry.second || !entry.second->touched;
  });
}

void WaterSurfaceRenderer3D::clear() {
  impl_->geometry.clear();
  if (impl_->program) {
    impl_->resources.destroy(impl_->program);
    impl_->program = {};
  }
  for (auto &sampler : impl_->samplers) {
    if (sampler) {
      impl_->resources.destroy(sampler);
      sampler = {};
    }
  }
  for (auto &uniform : impl_->uniforms) {
    if (uniform) {
      impl_->resources.destroy(uniform);
      uniform = {};
    }
  }
}

bool WaterSurfaceRenderer3D::draw(std::span<const VisibleMesh3D> surfaces,
                                  const BgfxCameraFrame3D &frame,
                                  const WaterSceneCapture3D &capture,
                                  TextureHandle environmentPanorama,
                                  const SceneLighting3D &lighting,
                                  float elapsedSeconds, std::string &error) {
  if (surfaces.empty()) {
    return true;
  }
  if (!capture.color || !capture.linearDepth || capture.width == 0 ||
      capture.height == 0) {
    error = "Water rendering requires complete opaque color/depth snapshots.";
    return false;
  }
  if (!initialize(error)) {
    return false;
  }
  const auto forward = normalized(frame.forward, {0, 0, -1});
  const auto right = normalized(cross(forward, frame.up), {1, 0, 0});
  const auto up = normalized(cross(right, forward), {0, 1, 0});
  const float aspect = float(capture.width) / capture.height;
  const float verticalExtent =
      frame.camera.perspective
          ? std::tan(frame.camera.fov * std::numbers::pi_v<float> / 360.0F)
          : frame.camera.orthographicSize;
  for (const auto &visible : surfaces) {
    const auto *water =
        visible.entity
            ->component<terrain_detail::TerrainGeneratedWaterSurface>();
    const auto *mesh = visible.entity->component<MeshRendererComponent>();
    if (!water || !mesh) {
      error =
          "The water pass received a surface without native water metadata.";
      return false;
    }
    auto *geometry = impl_->prepare(*visible.entity, *water, error);
    if (!geometry) {
      return false;
    }
    auto transform = visible.transform;
    transform.scale = {transform.scale.x * mesh->size.x,
                       transform.scale.y * mesh->size.y,
                       transform.scale.z * mesh->size.z};
    const auto localCamera = inverseTransformPoint3D(transform, frame.position);
    const auto localForward = inverseTransformVector3D(transform, forward);
    const auto &appearance = water->appearance;
    const std::array<std::array<float, 4>, uniformNames.size()> values{
        {{frame.position.x, frame.position.y, frame.position.z, 0},
         {forward.x, forward.y, forward.z, frame.camera.farClip},
         {right.x, right.y, right.z, 0},
         {up.x, up.y, up.z, 0},
         {1.0F / capture.width, 1.0F / capture.height,
          capture.originBottomLeft ? 1.0F : 0.0F,
          environmentPanorama ? 1.0F : 0.0F},
         {0.5F / (verticalExtent * aspect), 0.5F / verticalExtent,
          frame.camera.nearClip, frame.camera.perspective ? 1.0F : 0.0F},
         {localCamera.x, localCamera.y, localCamera.z,
          std::hypot(localForward.x, localForward.y, localForward.z)},
         linearColor(appearance.shallowColor),
         linearColor(appearance.deepColor),
         {appearance.absorptionDistance, appearance.roughness, 0, 0},
         {appearance.waveAmplitude, appearance.waveLength, appearance.waveSpeed,
          appearance.normalStrength},
         {appearance.foamWidth, appearance.foamStrength, elapsedSeconds, 0},
         linearColor(appearance.foamColor),
         lighting.direction,
         lighting.directionalColor,
         lighting.ambient}};
    std::array<DrawUniformValue, uniformNames.size()> uniforms;
    for (std::size_t index = 0; index < uniforms.size(); ++index) {
      uniforms[index] = {.handle = impl_->uniforms[index],
                         .values = values[index]};
    }
    const std::array textures{
        DrawTextureBinding{.stage = 1,
                           .texture = capture.linearDepth,
                           .sampler = impl_->samplers[1]},
        DrawTextureBinding{.stage = 2,
                           .texture = environmentPanorama ? environmentPanorama
                                                          : capture.color,
                           .sampler = impl_->samplers[2]}};
    if (!impl_->commands.submit(
            {.viewId = frame.viewId,
             .vertices = {.handle = geometry->vertices,
                          .count = geometry->vertexCount},
             .indices = {.handle = geometry->indices,
                         .count = geometry->indexCount},
             .program = impl_->program,
             .texture = capture.color,
             .sampler = impl_->samplers[0],
             .state = {.blend = BlendMode::Opaque,
                       .depthTest = DepthTest::Less,
                       .cull = CullMode::None,
                       .writeDepth = false,
                       .multisampling = true},
             .transform = composeMeshTransform3D(visible.transform, mesh->size),
             .uniforms = uniforms,
             .textures = textures},
            error)) {
      return false;
    }
    ++impl_->statistics.draws;
    impl_->statistics.triangles += geometry->indexCount / 3;
  }
  return true;
}

const WaterSurfaceStatistics3D &WaterSurfaceRenderer3D::statistics() const {
  return impl_->statistics;
}

} // namespace demi::runtime::render
