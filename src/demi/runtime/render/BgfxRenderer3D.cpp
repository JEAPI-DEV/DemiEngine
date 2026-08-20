#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"

#include "demi/runtime/render/bgfx3d/DebugGeometry3D.h"

#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"
#include "demi/runtime/render/bgfx3d/MeshTransform3D.h"
#include "demi/runtime/render/bgfx3d/PrimitiveMeshFactory3D.h"
#include "demi/runtime/render/bgfx3d/SceneLighting3D.h"
#include "demi/runtime/render/bgfx3d/WorldTextProjection3D.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ParticleEmitter3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/PostProcessStackComponent.h"

#include <algorithm>
#include <bit>
#include <unordered_set>

namespace demi::runtime::render {
namespace {

Point3D point(const Vec3 value) {
  return {.x = value.x, .y = value.y, .z = value.z};
}

void hashValue(std::uint64_t &hash, const std::uint32_t value) {
  constexpr std::uint64_t Prime = 1099511628211ULL;
  for (unsigned shift = 0; shift < 32; shift += 8) {
    hash ^= (value >> shift) & 0xffU;
    hash *= Prime;
  }
}

std::uint64_t meshCacheRevision(const MeshRendererComponent &mesh) {
  // Geometry mutation APIs advance revision. Keep this key constant-size so
  // resident procedural meshes do not rescan all vertex data every camera.
  std::uint64_t hash = 14695981039346656037ULL;
  hashValue(hash, static_cast<std::uint32_t>(mesh.revision));
  hashValue(hash, static_cast<std::uint32_t>(mesh.revision >> 32U));
  hashValue(hash, static_cast<std::uint32_t>(mesh.vertices.size()));
  hashValue(hash, static_cast<std::uint32_t>(mesh.normals.size()));
  hashValue(hash, static_cast<std::uint32_t>(mesh.uvs.size()));
  for (const char value : mesh.shape)
    hashValue(hash, static_cast<unsigned char>(value));
  return hash;
}

float debugModeValue(const std::string &mode) {
  if (mode == "normals")
    return 1.0F;
  if (mode == "uv")
    return 2.0F;
  if (mode == "alpha")
    return 3.0F;
  if (mode == "lighting")
    return 4.0F;
  if (mode == "overdraw")
    return 5.0F;
  if (mode == "instancing")
    return 6.0F;
  return 0.0F;
}

} // namespace

BgfxRenderer3D::BgfxRenderer3D(GpuResources &resources,
                               RenderCommands &commands)
    : resources_(resources), commands_(commands),
      primitives_(resources, commands), postProcess_(resources, commands),
      particleRenderer_(resources, commands), overlay_(resources, commands),
      textures_(resources), materials_(resources) {}

BgfxRenderer3D::~BgfxRenderer3D() { shutdown(); }

bool BgfxRenderer3D::initialize(std::string &error) {
  if (initialized_)
    return true;
  if (!primitives_.initialize(error))
    return false;
  if (!overlay_.initialize(error)) {
    primitives_.shutdown();
    return false;
  }
  if (!postProcess_.initialize(error)) {
    overlay_.shutdown();
    primitives_.shutdown();
    return false;
  }
  if (!particleRenderer_.initialize(error)) {
    postProcess_.shutdown();
    overlay_.shutdown();
    primitives_.shutdown();
    return false;
  }
  meshProgram_ = resources_.createBuiltinProgram(BuiltinProgram::Lit3D, error);
  instancedMeshProgram_ =
      resources_.createBuiltinProgram(BuiltinProgram::Lit3DInstanced, error);
  meshSampler_ = resources_.createSampler("s_texColor", error);
  tintUniform_ =
      resources_.createUniform("u_tint", UniformType::Vec4, 1, error);
  alphaCutoffUniform_ =
      resources_.createUniform("u_alphaCutoff", UniformType::Vec4, 1, error);
  debugModeUniform_ =
      resources_.createUniform("u_debugMode", UniformType::Vec4, 1, error);
  lightDirectionUniform_ =
      resources_.createUniform("u_lightDirection", UniformType::Vec4, 1, error);
  lightColorUniform_ =
      resources_.createUniform("u_lightColor", UniformType::Vec4, 1, error);
  ambientColorUniform_ =
      resources_.createUniform("u_ambientColor", UniformType::Vec4, 1, error);
  pointPositionRangeUniform_ = resources_.createUniform(
      "u_pointPositionRange", UniformType::Vec4, 4, error);
  pointColorIntensityUniform_ = resources_.createUniform(
      "u_pointColorIntensity", UniformType::Vec4, 4, error);
  spotPositionRangeUniform_ = resources_.createUniform(
      "u_spotPositionRange", UniformType::Vec4, 4, error);
  spotDirectionOuterUniform_ = resources_.createUniform(
      "u_spotDirectionOuter", UniformType::Vec4, 4, error);
  spotColorIntensityUniform_ = resources_.createUniform(
      "u_spotColorIntensity", UniformType::Vec4, 4, error);
  spotInnerUniform_ =
      resources_.createUniform("u_spotInner", UniformType::Vec4, 4, error);
  constexpr std::array<std::byte, 4> White{std::byte{0xff}, std::byte{0xff},
                                           std::byte{0xff}, std::byte{0xff}};
  whiteTexture_ = resources_.createTexture({.width = 1,
                                            .height = 1,
                                            .format = TextureFormat::RGBA8,
                                            .data = White,
                                            .filter = TextureFilter::Nearest,
                                            .wrap = TextureWrap::Clamp,
                                            .debugName = "3D white fallback"},
                                           error);
  if (!meshProgram_ || !instancedMeshProgram_ || !meshSampler_ ||
      !tintUniform_ || !alphaCutoffUniform_ || !debugModeUniform_ ||
      !whiteTexture_ || !lightDirectionUniform_ || !lightColorUniform_ ||
      !ambientColorUniform_ || !pointPositionRangeUniform_ ||
      !pointColorIntensityUniform_ || !spotPositionRangeUniform_ ||
      !spotDirectionOuterUniform_ || !spotColorIntensityUniform_ ||
      !spotInnerUniform_) {
    shutdown();
    return false;
  }
  initialized_ = true;
  return true;
}

void BgfxRenderer3D::shutdown() {
  dynamicMeshes_.clear();
  modelMeshes_.clear();
  animatedModels_.clear();
  modelTextures_.clear();
  modelUnlit_.clear();
  materials_.clear();
  for (const auto &[id, target] : renderTargets_) {
    static_cast<void>(id);
    if (target.handles.frameBuffer)
      resources_.destroy(target.handles.frameBuffer);
    if (target.handles.depth)
      resources_.destroy(target.handles.depth);
    if (target.handles.color)
      resources_.destroy(target.handles.color);
  }
  renderTargets_.clear();
  particles_.clear();
  textures_.clear();
  if (whiteTexture_)
    resources_.destroy(whiteTexture_);
  if (meshSampler_)
    resources_.destroy(meshSampler_);
  for (const UniformHandle uniform :
       {tintUniform_, alphaCutoffUniform_, debugModeUniform_,
        lightDirectionUniform_, lightColorUniform_, ambientColorUniform_,
        pointPositionRangeUniform_, pointColorIntensityUniform_,
        spotPositionRangeUniform_, spotDirectionOuterUniform_,
        spotColorIntensityUniform_, spotInnerUniform_})
    if (uniform)
      resources_.destroy(uniform);
  if (meshProgram_)
    resources_.destroy(meshProgram_);
  if (instancedMeshProgram_)
    resources_.destroy(instancedMeshProgram_);
  whiteTexture_ = {};
  meshSampler_ = {};
  tintUniform_ = {};
  alphaCutoffUniform_ = {};
  debugModeUniform_ = {};
  lightDirectionUniform_ = {};
  lightColorUniform_ = {};
  ambientColorUniform_ = {};
  pointPositionRangeUniform_ = {};
  pointColorIntensityUniform_ = {};
  spotPositionRangeUniform_ = {};
  spotDirectionOuterUniform_ = {};
  spotColorIntensityUniform_ = {};
  spotInnerUniform_ = {};
  meshProgram_ = {};
  instancedMeshProgram_ = {};
  particleRenderer_.shutdown();
  postProcess_.shutdown();
  overlay_.shutdown();
  primitives_.shutdown();
  statistics_.reset();
  initialized_ = false;
}

bool BgfxRenderer3D::renderFrame(const World &world,
                                 const BgfxCameraFrame3D &frame,
                                 const float deltaSeconds, std::string &error) {
  if (!initialized_) {
    error = "BgfxRenderer3D must be initialized before rendering.";
    return false;
  }
  const Vec3 target{frame.position.x + frame.forward.x,
                    frame.position.y + frame.forward.y,
                    frame.position.z + frame.forward.z};
  const auto renderTarget = renderTargets_.find(frame.camera.renderTarget);
  if (!frame.camera.renderTarget.empty() &&
      renderTarget == renderTargets_.end()) {
    error =
        "Camera " +
        (frame.cameraId.empty() ? std::string("<unnamed>") : frame.cameraId) +
        " references an unloaded render target: " + frame.camera.renderTarget;
    return false;
  }
  const bool authoredOffscreen = renderTarget != renderTargets_.end();
  const bool applyPostProcess = hasPostProcessEffects(frame.postProcess);
  const float renderScale =
      authoredOffscreen ? 1.0F
                        : std::clamp(frame.camera.renderScale, 0.25F, 2.0F);
  const std::uint16_t renderWidth =
      authoredOffscreen
          ? renderTarget->second.width
          : static_cast<std::uint16_t>(
                std::clamp(std::lround(static_cast<float>(frame.viewportWidth) *
                                       renderScale),
                           1L, static_cast<long>(UINT16_MAX)));
  const std::uint16_t renderHeight =
      authoredOffscreen
          ? renderTarget->second.height
          : static_cast<std::uint16_t>(std::clamp(
                std::lround(static_cast<float>(frame.viewportHeight) *
                            renderScale),
                1L, static_cast<long>(UINT16_MAX)));
  RenderTargetHandles sourceTarget;
  if (authoredOffscreen)
    sourceTarget = renderTarget->second.handles;
  else if (applyPostProcess)
    sourceTarget = postProcess_.scratchTarget(
        std::max<std::uint16_t>(renderWidth, 1),
        std::max<std::uint16_t>(renderHeight, 1), error);
  const bool offscreen = authoredOffscreen || applyPostProcess;
  if (offscreen && (!sourceTarget.frameBuffer || !sourceTarget.color)) {
    if (error.empty())
      error = "Could not create the camera's offscreen render surface.";
    return false;
  }
  if (frame.updateContent &&
      !primitives_.begin(
          View3DConfig{
              .id = frame.viewId,
              .x = offscreen ? std::uint16_t{0} : frame.viewportX,
              .y = offscreen ? std::uint16_t{0} : frame.viewportY,
              .width = std::max<std::uint16_t>(renderWidth, 1),
              .height = std::max<std::uint16_t>(renderHeight, 1),
              .clearRgba = packClearColorRgba8(frame.camera.clearColor),
              .eye = point(frame.position),
              .target = point(target),
              .up = point(frame.up),
              .verticalFovDegrees = frame.camera.fov,
              .nearClip = frame.camera.nearClip,
              .farClip = frame.camera.farClip,
              .orthographicSize = frame.camera.orthographicSize,
              .perspective = frame.camera.perspective,
              .clearColor = frame.camera.clearMode == "color",
              .clearDepth = frame.camera.clearMode != "none",
              .frameBuffer =
                  offscreen ? sourceTarget.frameBuffer : FrameBufferHandle{},
          },
          error))
    return false;

  std::unordered_set<std::string> liveDynamicMeshes;
  // Cache ownership follows entity lifetime, not camera visibility. Otherwise
  // leaving and re-entering the frustum would destroy and re-upload chunk
  // meshes, turning culling into a camera-movement hitch.
  for (const Entity &entity : world.entities) {
    const auto *mesh = entity.component<MeshRendererComponent>();
    if (mesh == nullptr)
      continue;
    if (!mesh->vertices.empty() || mesh->model.empty() ||
        entity.component<AnimationPlayer3DComponent>() != nullptr)
      liveDynamicMeshes.insert(entity.id);
  }
  const SceneLighting3D lighting =
      collectSceneLighting3D(world, frame.camera.renderMask);
  const std::array<float, 4> whiteTint{1.0F, 1.0F, 1.0F, 1.0F};
  const std::array<float, 4> noAlphaCutoff{};
  const std::array<float, 4> debugMode{debugModeValue(frame.camera.debugMode),
                                       0.0F, 0.0F, 0.0F};
  const std::array<float, 16> disabledArrayLights{};
  const std::array<DrawUniformValue, 12> lightingUniforms{{
      {.handle = tintUniform_, .values = whiteTint},
      {.handle = alphaCutoffUniform_, .values = noAlphaCutoff},
      {.handle = debugModeUniform_, .values = debugMode},
      {.handle = lightDirectionUniform_, .values = lighting.direction},
      {.handle = lightColorUniform_, .values = lighting.directionalColor},
      {.handle = ambientColorUniform_, .values = lighting.ambient},
      {.handle = pointPositionRangeUniform_,
       .values = lighting.pointPositionRange,
       .count = 4},
      {.handle = pointColorIntensityUniform_,
       .values = lighting.pointColorIntensity,
       .count = 4},
      {.handle = spotPositionRangeUniform_,
       .values = lighting.spotPositionRange,
       .count = 4},
      {.handle = spotDirectionOuterUniform_,
       .values = lighting.spotDirectionOuter,
       .count = 4},
      {.handle = spotColorIntensityUniform_,
       .values = lighting.spotColorIntensity,
       .count = 4},
      {.handle = spotInnerUniform_, .values = lighting.spotInner, .count = 4},
  }};
  struct InstanceGroup {
    const GpuMesh3D *mesh = nullptr;
    TextureHandle texture;
    std::array<float, 4> tint{1.0F, 1.0F, 1.0F, 1.0F};
    bool unlit = false;
    std::vector<std::array<float, 16>> transforms;
  };
  std::unordered_map<std::string, InstanceGroup> instanceGroups;
  std::uint32_t bufferedDraws = 0;
  std::uint32_t bufferedTriangles = 0;
  SceneVisibility3D visibility;
  if (frame.updateContent) {
    const auto extractionStarted = std::chrono::steady_clock::now();
    visibility = extractVisibleMeshes3D(world, frame, &extractionJobs_);
    lastExtractionMilliseconds_ =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - extractionStarted)
            .count();
  } else {
    lastExtractionMilliseconds_ = 0.0;
  }
  if (frame.updateContent)
    for (const VisibleMesh3D &visible : visibility.meshes) {
      const Entity &entity = *visible.entity;
      const auto *mesh = entity.component<MeshRendererComponent>();
      const WorldTransform3D &transform = visible.transform;
      const std::uint32_t color = packVertexColorRgba8(mesh->color);
      const std::array<float, 4> entityTint{mesh->color.r, mesh->color.g,
                                            mesh->color.b, mesh->color.a};
      const MaterialBinding *material = materials_.find(mesh->material);
      const ProgramHandle program = material != nullptr && material->program
                                        ? material->program
                                        : meshProgram_;
      DrawState state =
          material != nullptr
              ? material->state
              : DrawState{.blend = BlendMode::Opaque,
                          .depthTest = DepthTest::Less,
                          .cull = CullMode::None,
                          .topology = PrimitiveTopology::Triangles,
                          .writeDepth = true};
      if (frame.camera.debugMode == "overdraw") {
        state.blend = BlendMode::Additive;
        state.depthTest = DepthTest::Always;
        state.writeDepth = false;
      }
      std::vector<DrawUniformValue> drawUniforms(lightingUniforms.begin(),
                                                 lightingUniforms.end());
      drawUniforms.front().values = entityTint;
      const std::array<float, 4> alphaCutoff{
          material == nullptr ? 0.0F : material->alphaCutoff, 0.0F, 0.0F, 0.0F};
      drawUniforms[1].values = alphaCutoff;
      const auto modelLighting = modelUnlit_.find(mesh->model);
      const bool modelUnlit =
          modelLighting != modelUnlit_.end() && modelLighting->second;
      const std::array<float, 4> unlitAmbient{1.0F, 1.0F, 1.0F, 1.0F};
      const std::array<float, 4> noLight{};
      if (modelUnlit) {
        drawUniforms[3].values = noLight;
        drawUniforms[4].values = noLight;
        drawUniforms[5].values = unlitAmbient;
        for (std::size_t lightUniform = 6; lightUniform < 12; ++lightUniform)
          drawUniforms[lightUniform].values = disabledArrayLights;
      }
      if (material != nullptr)
        drawUniforms.insert(drawUniforms.end(), material->uniforms.begin(),
                            material->uniforms.end());
      bool queued = false;
      if (!mesh->vertices.empty()) {
        const std::uint64_t signature = meshCacheRevision(*mesh);
        auto &cached = dynamicMeshes_[entity.id];
        if (!cached)
          cached = std::make_unique<CachedMesh>(resources_);
        if (cached->signature != signature || !cached->gpu.valid()) {
          if (!cached->gpu.upload(mesh->vertices, mesh->uvs, {}, 0xffffffffU,
                                  error)) {
            error = entity.id + ": " + error;
            return false;
          }
          cached->signature = signature;
        }
        std::string textureId = mesh->texture;
        if (textureId.empty() && material != nullptr)
          textureId = material->albedoTexture;
        const TextureView2D texture = textures_.find(textureId);
        queued = cached->gpu.draw(
            commands_, frame.viewId, program,
            texture.handle ? texture.handle : whiteTexture_, meshSampler_,
            composeMeshTransform3D(transform, mesh->size), state, error,
            drawUniforms);
        if (queued) {
          ++bufferedDraws;
          bufferedTriangles += cached->gpu.indexCount() / 3U;
        }
      } else if (!mesh->model.empty()) {
        const auto cached = modelMeshes_.find(mesh->model);
        if (cached == modelMeshes_.end()) {
          error = "No migrated GPU model is loaded for " + mesh->model + ".";
          return false;
        }
        const CachedMesh *drawMesh = cached->second.get();
        const auto *player = entity.component<AnimationPlayer3DComponent>();
        if (player != nullptr) {
          const auto source = animatedModels_.find(mesh->model);
          if (source == animatedModels_.end()) {
            error = "No animation clips are loaded for " + mesh->model + ".";
            return false;
          }
          const int clip = source->second.clipIndex(player->clipName, 0);
          std::uint64_t signature = 14695981039346656037ULL;
          hashValue(signature, static_cast<std::uint32_t>(clip));
          hashValue(signature, std::bit_cast<std::uint32_t>(player->time));
          hashValue(signature, color);
          auto &animatedMesh = dynamicMeshes_[entity.id];
          if (!animatedMesh)
            animatedMesh = std::make_unique<CachedMesh>(resources_);
          if (animatedMesh->signature != signature ||
              !animatedMesh->gpu.valid()) {
            std::vector<Vec3> positions;
            if (!source->second.samplePositions(
                    clip, player->time, player->loop, positions, error)) {
              error = entity.id + ": " + error;
              return false;
            }
            std::vector<Vec2> textureCoordinates;
            std::vector<std::uint32_t> vertexColors;
            textureCoordinates.reserve(source->second.vertices.size());
            vertexColors.reserve(source->second.vertices.size());
            for (const assets::GltfSkinnedVertex3D &vertex :
                 source->second.vertices) {
              textureCoordinates.push_back(vertex.uv);
              vertexColors.push_back(packVertexColorRgba8(vertex.color));
            }
            if (!animatedMesh->gpu.upload(positions, textureCoordinates,
                                          source->second.indices, 0xffffffffU,
                                          error, {}, vertexColors)) {
              error = entity.id + ": " + error;
              return false;
            }
            animatedMesh->signature = signature;
          }
          drawMesh = animatedMesh.get();
        }
        const auto modelTexture = modelTextures_.find(mesh->model);
        const std::string textureId =
            material != nullptr && !material->albedoTexture.empty()
                ? material->albedoTexture
                : (modelTexture == modelTextures_.end() ? std::string{}
                                                        : modelTexture->second);
        const TextureView2D texture = textures_.find(textureId);
        const TextureHandle resolvedTexture =
            texture.handle ? texture.handle : whiteTexture_;
        if (player == nullptr && material == nullptr) {
          const std::string groupKey =
              mesh->model + "\n" + mesh->material + "\n" +
              std::to_string(color) + "\n" +
              std::to_string(resolvedTexture.index) + ":" +
              std::to_string(resolvedTexture.generation);
          auto &[groupMesh, groupTexture, groupTint, groupUnlit, transforms] =
              instanceGroups[groupKey];
          groupMesh = &drawMesh->gpu;
          groupTexture = resolvedTexture;
          groupTint = entityTint;
          groupUnlit = modelUnlit;
          transforms.push_back(composeMeshTransform3D(transform, mesh->size));
          queued = true;
        } else {
          queued = drawMesh->gpu.draw(
              commands_, frame.viewId, program, resolvedTexture, meshSampler_,
              composeMeshTransform3D(transform, mesh->size), state, error,
              drawUniforms);
          if (queued) {
            ++bufferedDraws;
            bufferedTriangles += drawMesh->gpu.indexCount() / 3U;
          }
        }
      } else {
        const std::uint64_t signature = meshCacheRevision(*mesh);
        auto &cached = dynamicMeshes_[entity.id];
        if (!cached)
          cached = std::make_unique<CachedMesh>(resources_);
        if (cached->signature != signature || !cached->gpu.valid()) {
          PrimitiveMeshData3D primitive;
          if (!createPrimitiveMesh3D(mesh->shape, primitive)) {
            error = entity.id + ": unsupported primitive shape '" +
                    mesh->shape + "'.";
            return false;
          }
          if (!cached->gpu.upload(primitive.positions,
                                  primitive.textureCoordinates,
                                  primitive.indices, 0xffffffffU, error)) {
            error = entity.id + ": " + error;
            return false;
          }
          cached->signature = signature;
        }
        std::string textureId = mesh->texture;
        if (textureId.empty() && material != nullptr)
          textureId = material->albedoTexture;
        const TextureView2D texture = textures_.find(textureId);
        queued = cached->gpu.draw(
            commands_, frame.viewId, program,
            texture.handle ? texture.handle : whiteTexture_, meshSampler_,
            composeMeshTransform3D(transform, mesh->size), state, error,
            drawUniforms);
        if (queued) {
          ++bufferedDraws;
          bufferedTriangles += cached->gpu.indexCount() / 3U;
        }
      }
      if (!queued) {
        error = "Could not queue 3D geometry for entity " + entity.id + ".";
        return false;
      }
    }
  if (frame.updateContent) {
    DrawState state{.blend = BlendMode::Opaque,
                    .depthTest = DepthTest::Less,
                    .cull = CullMode::None,
                    .topology = PrimitiveTopology::Triangles,
                    .writeDepth = true};
    if (frame.camera.debugMode == "overdraw") {
      state.blend = BlendMode::Additive;
      state.depthTest = DepthTest::Always;
      state.writeDepth = false;
    }
    for (const auto &[key, group] : instanceGroups) {
      static_cast<void>(key);
      std::array<DrawUniformValue, 12> groupUniforms = lightingUniforms;
      groupUniforms.front().values = group.tint;
      const std::array<float, 4> groupingMode{
          debugMode[0], group.transforms.size() > 1U ? 1.0F : 0.0F, 0.0F, 0.0F};
      groupUniforms[2].values = groupingMode;
      const std::array<float, 4> unlitAmbient{1.0F, 1.0F, 1.0F, 1.0F};
      const std::array<float, 4> noLight{};
      if (group.unlit) {
        groupUniforms[3].values = noLight;
        groupUniforms[4].values = noLight;
        groupUniforms[5].values = unlitAmbient;
        for (std::size_t lightUniform = 6; lightUniform < 12; ++lightUniform)
          groupUniforms[lightUniform].values = disabledArrayLights;
      }
      const bool queued =
          group.transforms.size() == 1U
              ? group.mesh->draw(commands_, frame.viewId, meshProgram_,
                                 group.texture, meshSampler_,
                                 group.transforms.front(), state, error,
                                 groupUniforms)
              : group.mesh->drawInstanced(commands_, frame.viewId,
                                          instancedMeshProgram_, group.texture,
                                          meshSampler_, group.transforms, state,
                                          error, groupUniforms);
      if (!queued)
        return false;
      ++bufferedDraws;
      bufferedTriangles += group.mesh->indexCount() / 3U *
                           static_cast<std::uint32_t>(group.transforms.size());
    }
  }
  if (frame.updateContent)
    particles_.update(world, deltaSeconds);
  const auto particleData = frame.updateContent
                                ? particles_.renderData(frame.camera.renderMask)
                                : std::vector<ParticleRenderData3D>{};
  std::vector<ParticleBillboardDraw3D> particleDraws;
  particleDraws.reserve(particleData.size());
  for (const ParticleRenderData3D &particle : particleData) {
    std::string textureId = particle.texture;
    const MaterialBinding *material = materials_.find(particle.material);
    if (textureId.empty() && material != nullptr)
      textureId = material->albedoTexture;
    const TextureView2D texture = textures_.find(textureId);
    const BlendMode blend =
        material == nullptr ? BlendMode::Alpha : material->state.blend;
    particleDraws.push_back(
        {.particle = particle,
         .texture = texture.handle ? texture.handle : whiteTexture_,
         .blend = blend});
  }
  std::ranges::stable_sort(
      particleDraws, [&frame](const ParticleBillboardDraw3D &left,
                              const ParticleBillboardDraw3D &right) {
        if (left.particle.sortingOrder != right.particle.sortingOrder)
          return left.particle.sortingOrder < right.particle.sortingOrder;
        const auto distanceSquared = [&frame](const Vec3 position) {
          const float x = position.x - frame.position.x;
          const float y = position.y - frame.position.y;
          const float z = position.z - frame.position.z;
          return x * x + y * y + z * z;
        };
        return distanceSquared(left.particle.position) >
               distanceSquared(right.particle.position);
      });
  if (frame.updateContent) {
    std::erase_if(dynamicMeshes_, [&liveDynamicMeshes](const auto &entry) {
      return !liveDynamicMeshes.contains(entry.first);
    });
    DebugGeometry3DRequest debugRequest = frame.debugGeometry;
    debugRequest.forceColliders =
        debugRequest.forceColliders || frame.camera.debugMode == "colliders";
    debugRequest.bounds =
        debugRequest.bounds || frame.camera.debugMode == "bounds";
    if (!appendDebugGeometry3D(world, primitives_, debugRequest)) {
      error = "3D debug geometry exceeded the transient line capacity.";
      return false;
    }
    if (!primitives_.flush(error))
      return false;
  }
  if (!particleRenderer_.draw(frame.viewId, frame, particleDraws, error))
    return false;

  std::uint32_t overlayDrawCalls = 0;
  std::uint32_t overlayTriangles = 0;
  if (frame.updateContent && offscreen && frame.camera.renderHud &&
      frame.camera.renderHudToTarget) {
    if (!overlay_.beginOverlayRegion(
            static_cast<std::uint16_t>(frame.viewId + 1U), 0, 0, renderWidth,
            renderHeight, deltaSeconds, error, sourceTarget.frameBuffer))
      return false;
    BgfxCameraFrame3D targetFrame = frame;
    targetFrame.viewportWidth = renderWidth;
    targetFrame.viewportHeight = renderHeight;
    const bool rendered =
        overlay_.drawHud(world) &&
        overlay_.drawUi(projectWorldText3D(world, targetFrame));
    if (!rendered || !overlay_.endFrame(error)) {
      if (error.empty())
        error = "Could not queue the render-target HUD.";
      return false;
    }
    overlayDrawCalls += overlay_.statistics().drawCalls;
    overlayTriangles += overlay_.statistics().triangles;
  }

  if (offscreen) {
    if (applyPostProcess) {
      if (!postProcess_.present(frame, sourceTarget.color, {}, error))
        return false;
      ++overlayDrawCalls;
      overlayTriangles += 2;
    } else {
      if (!overlay_.beginOverlayRegion(
              static_cast<std::uint16_t>(frame.viewId + 2U), frame.viewportX,
              frame.viewportY, frame.viewportWidth, frame.viewportHeight,
              deltaSeconds, error))
        return false;
      ui::UiDocument presentation;
      presentation.canvasSize = {static_cast<float>(frame.viewportWidth),
                                 static_cast<float>(frame.viewportHeight)};
      ui::UiNode image;
      image.id = "camera_target:" + frame.camera.renderTarget;
      image.type = "image";
      image.texture = frame.camera.renderTarget;
      image.color = {1.0F, 1.0F, 1.0F, 1.0F};
      image.resolved = {0.0F, 0.0F, presentation.canvasSize.x,
                        presentation.canvasSize.y};
      presentation.nodes.push_back(std::move(image));
      if (!overlay_.drawUi(presentation) || !overlay_.endFrame(error)) {
        if (error.empty())
          error = "Could not present a 3D render target.";
        return false;
      }
      overlayDrawCalls += overlay_.statistics().drawCalls;
      overlayTriangles += overlay_.statistics().triangles;
    }
    if (frame.camera.renderHud && !frame.camera.renderHudToTarget) {
      if (!overlay_.beginOverlayRegion(
              static_cast<std::uint16_t>(frame.viewId + 3U), frame.viewportX,
              frame.viewportY, frame.viewportWidth, frame.viewportHeight,
              deltaSeconds, error))
        return false;
      const bool rendered = overlay_.drawHud(world) &&
                            overlay_.drawUi(projectWorldText3D(world, frame));
      if (!rendered || !overlay_.endFrame(error)) {
        if (error.empty())
          error = "Could not render the camera HUD.";
        return false;
      }
      overlayDrawCalls += overlay_.statistics().drawCalls;
      overlayTriangles += overlay_.statistics().triangles;
    }
  } else if (frame.camera.renderHud) {
    if (!overlay_.beginOverlayRegion(
            static_cast<std::uint16_t>(frame.viewId + 1U), frame.viewportX,
            frame.viewportY, frame.viewportWidth, frame.viewportHeight,
            deltaSeconds, error))
      return false;
    const bool hudRendered = !frame.camera.renderHud || overlay_.drawHud(world);
    const bool worldTextRendered =
        !frame.camera.renderHud ||
        overlay_.drawUi(projectWorldText3D(world, frame));
    const bool hudFlushed = overlay_.endFrame(error);
    if (!hudRendered || !worldTextRendered) {
      error = "Could not queue the 3D scene overlay.";
      return false;
    }
    if (!hudFlushed)
      return false;
    overlayDrawCalls += overlay_.statistics().drawCalls;
    overlayTriangles += overlay_.statistics().triangles;
  }

  statistics_.reset();
  statistics_.batches = bufferedDraws + primitives_.statistics().drawCalls +
                        particleRenderer_.statistics().drawCalls +
                        overlayDrawCalls;
  statistics_.triangles =
      bufferedTriangles + primitives_.statistics().triangles +
      particleRenderer_.statistics().triangles + overlayTriangles;
  statistics_.particles = static_cast<std::uint32_t>(particleData.size());
  statistics_.consideredMeshes = visibility.considered;
  statistics_.visibleMeshes = visibility.meshes.size();
  statistics_.culledMeshes = visibility.culled;
  return true;
}

} // namespace demi::runtime::render
