#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/destruction/DestructionWorld3D.h"
#include "demi/runtime/destruction/DetachedFragmentFade3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
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
#include <cstdlib>
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
                               RenderCommands &commands, bool enableGpuSkinning)
    : resources_(resources), shadows_(resources,commands), commands_(shadows_),
      primitives_(resources, commands_), postProcess_(resources, commands_),
      particleRenderer_(resources, commands_), overlay_(resources, commands_),
      textures_(resources), sky_(resources), materials_(resources), gpuSkinningRequested_(enableGpuSkinning),
      deformedMeshes_(resources), reliefMeshes_(resources) {}

BgfxRenderer3D::~BgfxRenderer3D() { shutdown(); }

bool BgfxRenderer3D::initialize(std::string &error) {
  if (initialized_)
    return true;
  if(!shadows_.initialize(error))return false;
  if (!primitives_.initialize(error)) {
    shutdown();
    return false;
  }
  if (!overlay_.initialize(error)) {
    shutdown();
    return false;
  }
  if (!postProcess_.initialize(error)) {
    shutdown();
    return false;
  }
  if (!particleRenderer_.initialize(error)) {
    shutdown();
    return false;
  }
  meshProgram_ = resources_.createBuiltinProgram(BuiltinProgram::Lit3D, error);
  instancedMeshProgram_ =
      resources_.createBuiltinProgram(BuiltinProgram::Lit3DInstanced, error);
  directionalMeshProgram_ = resources_.createBuiltinProgram(BuiltinProgram::Directional3D, error);
  directionalInstancedProgram_ = resources_.createBuiltinProgram(BuiltinProgram::Directional3DInstanced, error);
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
  if (!meshProgram_ || !instancedMeshProgram_ || !directionalMeshProgram_ ||
      !directionalInstancedProgram_ || !meshSampler_ ||
      !tintUniform_ || !alphaCutoffUniform_ || !debugModeUniform_ ||
      !whiteTexture_ || !lightDirectionUniform_ || !lightColorUniform_ ||
      !ambientColorUniform_ || !pointPositionRangeUniform_ ||
      !pointColorIntensityUniform_ || !spotPositionRangeUniform_ ||
      !spotDirectionOuterUniform_ || !spotColorIntensityUniform_ ||
      !spotInnerUniform_) {
    shutdown();
    return false;
  }
  const char *gpuOverride = std::getenv("DEMI_GPU_SKINNING");
  gpuSkinningEnabled_ = gpuSkinningRequested_ &&
      (!gpuOverride || std::string_view(gpuOverride) != "0") &&
      (resources_.shaderBackend() == "vulkan" || resources_.shaderBackend() == "noop");
  if (gpuSkinningEnabled_) {
    skinnedMeshProgram_ = resources_.createBuiltinProgram(BuiltinProgram::Lit3DSkinned, error);
    directionalSkinnedProgram_ = resources_.createBuiltinProgram(BuiltinProgram::Directional3DSkinned, error);
    skinMatricesUniform_ = resources_.createUniform("u_skinMatrices", UniformType::Matrix4, MaximumGpuSkinMatrices, error);
    skinImportUniform_ = resources_.createUniform("u_skinImport", UniformType::Matrix4, 1, error);
    if (!skinnedMeshProgram_ || !directionalSkinnedProgram_ || !skinMatricesUniform_ || !skinImportUniform_) {
      shutdown();
      return false;
    }
  }
  initialized_ = true;
  return true;
}

void BgfxRenderer3D::shutdown() {
  shadows_.shutdown();
  sky_.clear();
  for (const auto program : {directionalMeshProgram_, directionalInstancedProgram_, directionalSkinnedProgram_})
    if (program) resources_.destroy(program);
  directionalMeshProgram_ = {};
  directionalInstancedProgram_ = {};
  directionalSkinnedProgram_ = {};
  if (skinnedMeshProgram_) resources_.destroy(skinnedMeshProgram_);
  if (skinMatricesUniform_) resources_.destroy(skinMatricesUniform_);
  if (skinImportUniform_) resources_.destroy(skinImportUniform_);
  skinnedMeshProgram_ = {};
  skinMatricesUniform_ = {};
  skinImportUniform_ = {};
  deformedMeshes_.clear();
  reliefMeshes_.clear();
  dynamicMeshes_.clear();
  primitiveMeshes_.clear();
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
  if(!initialized_) {error="BgfxRenderer3D must be initialized before rendering.";return false;}
  const auto lighting=frame.lightingOverride.value_or(collectSceneLighting3D(world,frame.camera.renderMask));
  if(!shadows_.prepare(lighting,frame,error))return false;
  auto mainFrame=frame;
  if(shadows_.depthFrame()) {
    ProfileScope scope("Renderer3D.directional_shadow");
    if(!renderView(world,*shadows_.depthFrame(),0,error,true))return false;
    RuntimeProfiler::setGauge("Renderer3D.shadow_batches",statistics_.batches);
    ++mainFrame.viewId;
  } else RuntimeProfiler::setGauge("Renderer3D.shadow_batches",0);
  shadows_.receive();
  return renderView(world,mainFrame,deltaSeconds,error,false);
}

bool BgfxRenderer3D::renderView(const World &world,const BgfxCameraFrame3D &frame,
    float deltaSeconds,std::string &error,bool shadowPass) {
  if (!initialized_) {
    error = "BgfxRenderer3D must be initialized before rendering.";
    return false;
  }
  const Vec3 target{frame.position.x + frame.forward.x,
                    frame.position.y + frame.forward.y,
                    frame.position.z + frame.forward.z};
  const SceneLighting3D sceneLighting = collectSceneLighting3D(world, frame.camera.renderMask);
  const int msaaSamples=shadowPass?1:std::max(sceneLighting.msaaSamples,1);
  if(!shadowPass)postProcess_.setMsaaSamples(msaaSamples);
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
  if(authoredOffscreen && frame.updateContent &&
     !setRenderTargetSamples(renderTarget->second,frame.camera.renderTarget,msaaSamples,error))return false;
  const bool hostOffscreen = static_cast<bool>(frame.frameBuffer);
  const bool applyPostProcess = hasPostProcessEffects(frame.postProcess);
  const float renderScale =
      authoredOffscreen ? 1.0F
                        : std::clamp(frame.camera.renderScale, 0.25F, 2.0F);
  // Scaling needs an intermediate surface even without color effects. Drawing
  // a smaller view directly into the destination leaves its edges untouched.
  const bool resolveSurface = applyPostProcess || renderScale != 1.0F ||
      (!authoredOffscreen && msaaSamples!=frame.destinationSamples);
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
  sourceTarget.msaaSamples=frame.destinationSamples;
  if (authoredOffscreen)
    sourceTarget = renderTarget->second.handles;
  else if (resolveSurface)
    sourceTarget = postProcess_.scratchTarget(
        std::max<std::uint16_t>(renderWidth, 1),
        std::max<std::uint16_t>(renderHeight, 1), error, msaaSamples);
  else if (hostOffscreen)
    sourceTarget.frameBuffer = frame.frameBuffer;
  const bool offscreen = authoredOffscreen || hostOffscreen || resolveSurface;
  if(!shadowPass) {
    RuntimeProfiler::setGauge("Renderer3D.msaa_requested",msaaSamples);
    RuntimeProfiler::setGauge("Renderer3D.msaa_backend_request",sourceTarget.msaaSamples);
  }
  if (offscreen && (!sourceTarget.frameBuffer ||
                    (resolveSurface && !sourceTarget.color))) {
    if (error.empty())
      error = "Could not create the camera's offscreen render surface.";
    return false;
  }
  SceneVisibility3D visibility;
  auto cosmeticFragments = !shadowPass && frame.updateContent && world.destruction3D
      ? world.destruction3D->cosmeticFragments() : std::vector<CosmeticFragment3D>{};
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
  if (frame.updateContent && !prepareAnimatedMeshes(visibility.meshes, frame, error))
    return false;
  const bool hasFadingMeshes=std::ranges::any_of(visibility.meshes,[](const VisibleMesh3D &v){return v.entity->hasComponent<FragmentOpacity3D>();});
  if(hasFadingMeshes) std::stable_sort(visibility.meshes.begin(),visibility.meshes.end(),[&](const auto &a,const auto &b) {
    const auto depth=[&](const auto &v) {return (v.transform.position.x-frame.position.x)*frame.forward.x+(v.transform.position.y-frame.position.y)*frame.forward.y+(v.transform.position.z-frame.position.z)*frame.forward.z;};
    return depth(a)>depth(b);
  });
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
              .sequential = !cosmeticFragments.empty() || hasFadingMeshes,
          },
          error))
    return false;

  std::unordered_set<std::string> liveDynamicMeshes;
  std::unordered_set<std::string> liveDeformedMeshes;
  // Cache ownership follows entity lifetime, not camera visibility. Otherwise
  // leaving and re-entering the frustum would destroy and re-upload chunk
  // meshes, turning culling into a camera-movement hitch.
  for (const Entity &entity : world.entities) {
    const auto *mesh = entity.component<MeshRendererComponent>();
    if (mesh == nullptr)
      continue;
    if (!entityMeshDents3D(entity).empty())
      liveDeformedMeshes.insert(entity.id);
    if (!mesh->vertices.empty() ||
        entity.component<AnimationPlayer3DComponent>() != nullptr)
      liveDynamicMeshes.insert(entity.id);
  }
  reliefMeshes_.setRetentionBudget(sceneLighting.reliefCacheMeshes,sceneLighting.reliefImageCacheBytes);
  bool skyDrawn = false;
  if (frame.updateContent && frame.camera.perspective &&
      frame.camera.clearMode == "color" && !sceneLighting.skyTexture.empty()) {
    const auto texture = textures_.find(sceneLighting.skyTexture);
    if (!texture.handle) {
      error = "Environment sky texture is not loaded: " + sceneLighting.skyTexture;
      return false;
    }
    if (!sky_.draw(commands_, frame.viewId, texture.handle, frame.position, error))
      return false;
    skyDrawn = true;
  }
  const SceneLighting3D lighting =
      frame.lightingOverride
          ? *frame.lightingOverride
          : sceneLighting;
  const bool directionalOnly = !lighting.hasLocalLights();
  RuntimeProfiler::setGauge("Renderer3D.directional_shader", directionalOnly ? 1.0 : 0.0);
  const ProgramHandle defaultMeshProgram = directionalOnly ? directionalMeshProgram_ : meshProgram_;
  const ProgramHandle defaultInstancedProgram = directionalOnly ? directionalInstancedProgram_ : instancedMeshProgram_;
  const ProgramHandle defaultSkinnedProgram = directionalOnly ? directionalSkinnedProgram_ : skinnedMeshProgram_;
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
  reliefMeshes_.beginFrame();
  std::uint32_t bufferedDraws = skyDrawn ? 1U : 0U;
  std::uint32_t bufferedTriangles = skyDrawn ? 12U : 0U;
  std::uint32_t distanceCulled = 0;
  std::uint32_t mediumLodMeshes = 0;
  std::uint32_t lowLodMeshes = 0;
  for(int fadingPass=0;fadingPass<(hasFadingMeshes?2:1);++fadingPass) {
  if (frame.updateContent)
    for (const VisibleMesh3D &visible : visibility.meshes) {
      const auto *fade=visible.entity->component<FragmentOpacity3D>();
      if(bool(fade)!=bool(fadingPass) || (fade && shadowPass)) continue;
      const Entity &entity = *visible.entity;
      const auto *mesh = entity.component<MeshRendererComponent>();
      const auto dents = entityMeshDents3D(entity);
      const WorldTransform3D &transform = visible.transform;
      const auto *player = entity.component<AnimationPlayer3DComponent>();
      const ModelLodSelection lod =
          selectModelLod(*mesh, player, transform.position, frame.lodPosition.value_or(frame.position),
                         !dents.empty());
      if (lod.isCulled) {
        ++distanceCulled;
        continue;
      }
      const std::string &selectedModel = *lod.model;
      mediumLodMeshes += lod.level == 1 ? 1U : 0U;
      lowLodMeshes += lod.level == 2 ? 1U : 0U;
      const std::uint32_t color = packVertexColorRgba8(mesh->color);
      const std::array<float, 4> entityTint{mesh->color.r, mesh->color.g,
                                            mesh->color.b, mesh->color.a*(fade?fade->value:1.F)};
      const MaterialBinding *material = materials_.find(mesh->material);
      const ProgramHandle program = !shadowPass && material != nullptr && material->program
                                        ? material->program
                                        : defaultMeshProgram;
      DrawState state =
          material != nullptr
              ? material->state
              : DrawState{.blend = BlendMode::Opaque,
                          .depthTest = DepthTest::Less,
                          .cull = CullMode::None,
                          .topology = PrimitiveTopology::Triangles,
                          .writeDepth = true};
      if(fade) {state.blend=BlendMode::Alpha;state.writeDepth=false;}
      if(shadowPass && (state.blend!=BlendMode::Opaque || !state.writeDepth))continue;
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
      const auto modelLighting = modelUnlit_.find(selectedModel);
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
      if (const auto *relief=entity.component<SurfaceRelief3DComponent>(); relief && (!relief->heightMap.empty() || relief->tiles.x > 1 || relief->tiles.y > 1)) {
        if (player || !dents.empty() || !mesh->model.empty() || !mesh->vertices.empty() || mesh->shape!="cube") {
          error="SurfaceRelief3D requires an undeformed cube MeshRenderer";
          return false;
        }
        const auto *gpu=reliefMeshes_.get(*relief,mesh->size,error);
        if (!gpu) return false;
        std::string textureId=mesh->texture;
        if(textureId.empty() && material)textureId=material->albedoTexture;
        const auto texture=textures_.find(textureId);
        const auto resolved=texture.handle?texture.handle:whiteTexture_;
        if(!material && !fade) {
          const auto key="relief:"+std::to_string(reinterpret_cast<std::uintptr_t>(gpu))+":"+
              std::to_string(color)+":"+std::to_string(resolved.index)+":"+std::to_string(resolved.generation);
          auto &[groupMesh,groupTexture,groupTint,groupUnlit,transforms]=instanceGroups[key];
          groupMesh=gpu;groupTexture=resolved;groupTint=entityTint;groupUnlit=false;
          transforms.push_back(composeMeshTransform3D(transform,mesh->size));queued=true;
        } else {
          queued=gpu->draw(commands_,frame.viewId,program,resolved,meshSampler_,
                          composeMeshTransform3D(transform,mesh->size),state,error,drawUniforms);
          if(queued){++bufferedDraws;bufferedTriangles+=gpu->indexCount()/3U;}
        }
      } else if (!mesh->vertices.empty()) {
        const std::uint64_t signature = meshCacheRevision(*mesh);
        auto &cached = dynamicMeshes_[entity.id];
        if (!cached)
          cached = std::make_unique<CachedMesh>(resources_);
        if (!cached->animationModel.empty()) {
          cached->gpu.clear();
          cached->animationModel.clear();
          cached->skinPalette.clear();
        }
        if (dents.empty() &&
            (cached->signature != signature || !cached->gpu.valid())) {
          if (!cached->gpu.upload(mesh->vertices, mesh->uvs, {}, 0xffffffffU,
                                  error, mesh->normals)) {
            error = entity.id + ": " + error;
            return false;
          }
          cached->signature = signature;
        }
        std::string textureId = mesh->texture;
        if (textureId.empty() && material != nullptr)
          textureId = material->albedoTexture;
        const TextureView2D texture = textures_.find(textureId);
        const GpuMesh3D *drawMesh = &cached->gpu;
        if (!dents.empty()) {
          drawMesh = deformedMeshes_.get(entity.id, "inline", signature,
                                         mesh->vertices, mesh->uvs, {}, {},
                                         dents, error);
          if (!drawMesh)
            return false;
        }
        queued = drawMesh->draw(commands_, frame.viewId, program,
                                texture.handle ? texture.handle : whiteTexture_,
                                meshSampler_,
                                composeMeshTransform3D(transform, mesh->size),
                                state, error, drawUniforms);
        if (queued) {
          ++bufferedDraws;
          bufferedTriangles += drawMesh->indexCount() / 3U;
        }
      } else if (!selectedModel.empty()) {
        const auto cached = modelMeshes_.find(selectedModel);
        if (cached == modelMeshes_.end()) {
          error = "No migrated GPU model is loaded for " + selectedModel + ".";
          return false;
        }
        const CachedMesh *drawMesh = cached->second.get();
        const GpuSkinnedMesh3D *gpuSkin = nullptr;
        const std::vector<float> *skinPalette = nullptr;
        if (player != nullptr) {
          const auto animated = dynamicMeshes_.find(entity.id);
          if (animated == dynamicMeshes_.end()) {
            error = entity.id + ": Animated mesh preparation did not complete.";
            return false;
          }
          if (!animated->second->skinPalette.empty()) {
            gpuSkin = cached->second->gpuSkin.get();
            skinPalette = &animated->second->skinPalette;
            if (!gpuSkin) {
              error = "GPU skin geometry is unavailable for " + selectedModel;
              return false;
            }
          } else {
            if (!animated->second->gpu.valid()) {
              error = entity.id + ": CPU skin geometry is unavailable.";
              return false;
            }
            drawMesh = animated->second.get();
          }
        }
        const GpuMesh3D *drawGpu = &drawMesh->gpu;
        if (!dents.empty() && player == nullptr) {
          const auto &rest = cached->second->restGeometry;
          drawGpu = deformedMeshes_.get(entity.id, selectedModel, 0,
                                        rest.positions, rest.uvs, rest.indices,
                                        rest.colors, dents, error);
          if (!drawGpu)
            return false;
        }
        const auto modelTexture = modelTextures_.find(selectedModel);
        // Match primitive meshes: an entity texture overrides material and
        // embedded model albedo. Keep it in the instancing key below as well.
        std::string textureId = mesh->texture;
        if (textureId.empty() && material != nullptr)
          textureId = material->albedoTexture;
        if (textureId.empty() && modelTexture != modelTextures_.end())
          textureId = modelTexture->second;
        const TextureView2D texture = textures_.find(textureId);
        const TextureHandle resolvedTexture =
            texture.handle ? texture.handle : whiteTexture_;
        if (gpuSkin) {
          drawUniforms.push_back({.handle=skinMatricesUniform_, .values=*skinPalette,
              .count=static_cast<std::uint16_t>(skinPalette->size()/16)});
          drawUniforms.push_back({.handle=skinImportUniform_,
              .values=animatedModels_.at(selectedModel).importTransform});
          queued = gpuSkin->draw(commands_, frame.viewId, defaultSkinnedProgram,
              resolvedTexture, meshSampler_, composeMeshTransform3D(transform, mesh->size),
              state, drawUniforms, error);
          if (queued) {
            ++bufferedDraws;
            bufferedTriangles += gpuSkin->indexCount()/3;
          }
        } else if (player == nullptr && material == nullptr && dents.empty() && !fade) {
          const std::string groupKey =
              selectedModel + "\n" + mesh->material + "\n" +
              std::to_string(color) + "\n" +
              std::to_string(resolvedTexture.index) + ":" +
              std::to_string(resolvedTexture.generation);
          auto &[groupMesh, groupTexture, groupTint, groupUnlit, transforms] =
              instanceGroups[groupKey];
          groupMesh = drawGpu;
          groupTexture = resolvedTexture;
          groupTint = entityTint;
          groupUnlit = modelUnlit;
          transforms.push_back(composeMeshTransform3D(transform, mesh->size));
          queued = true;
        } else {
          queued = drawGpu->draw(commands_, frame.viewId, program,
                                 resolvedTexture, meshSampler_,
                                 composeMeshTransform3D(transform, mesh->size),
                                 state, error, drawUniforms);
          if (queued) {
            ++bufferedDraws;
            bufferedTriangles += drawGpu->indexCount() / 3U;
          }
        }
      } else {
        auto &cached = primitiveMeshes_[mesh->shape];
        if (!cached)
          cached = std::make_unique<CachedMesh>(resources_);
        if (!cached->gpu.valid()) {
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
        }
        std::string textureId = mesh->texture;
        if (textureId.empty() && material != nullptr)
          textureId = material->albedoTexture;
        const TextureView2D texture = textures_.find(textureId);
        const TextureHandle resolvedTexture =
            texture.handle ? texture.handle : whiteTexture_;
        if (material == nullptr && !fade) {
          const std::string groupKey =
              "primitive\n" + mesh->shape + "\n" + std::to_string(color) +
              "\n" + std::to_string(resolvedTexture.index) + ":" +
              std::to_string(resolvedTexture.generation);
          auto &[groupMesh, groupTexture, groupTint, groupUnlit, transforms] =
              instanceGroups[groupKey];
          groupMesh = &cached->gpu;
          groupTexture = resolvedTexture;
          groupTint = entityTint;
          groupUnlit = false;
          transforms.push_back(composeMeshTransform3D(transform, mesh->size));
          queued = true;
        } else {
          queued = cached->gpu.draw(
              commands_, frame.viewId, program, resolvedTexture, meshSampler_,
              composeMeshTransform3D(transform, mesh->size), state, error,
              drawUniforms);
          if (queued) {
            ++bufferedDraws;
            bufferedTriangles += cached->gpu.indexCount() / 3U;
          }
        }
      }
      if (!queued) {
        error = "Could not queue 3D geometry for entity " + entity.id + ".";
        return false;
      }
    }
  if (frame.updateContent && !fadingPass) {
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
              ? group.mesh->draw(commands_, frame.viewId, defaultMeshProgram,
                                 group.texture, meshSampler_,
                                 group.transforms.front(), state, error,
                                 groupUniforms)
              : group.mesh->drawInstanced(commands_, frame.viewId,
                                          defaultInstancedProgram, group.texture,
                                          meshSampler_, group.transforms, state,
                                          error, groupUniforms);
      if (!queued)
        return false;
      ++bufferedDraws;
      bufferedTriangles += group.mesh->indexCount() / 3U *
                           static_cast<std::uint32_t>(group.transforms.size());
    }
  }
  }
  if (!cosmeticFragments.empty() && !drawCosmeticDebris(cosmeticFragments, frame,
      lightingUniforms, defaultMeshProgram, bufferedDraws, bufferedTriangles, error)) return false;
  if (!shadowPass) RuntimeProfiler::setGauge("Renderer3D.cosmetic_fragments", cosmeticFragments.size());
  if (frame.updateContent && !shadowPass)
    particles_.update(world, deltaSeconds);
  const auto particleData = frame.updateContent && !shadowPass
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
    deformedMeshes_.retain(liveDeformedMeshes);
    std::erase_if(dynamicMeshes_, [&liveDynamicMeshes](const auto &entry) {
      return !liveDynamicMeshes.contains(entry.first);
    });
    DebugGeometry3DRequest debugRequest = frame.debugGeometry;
    debugRequest.forceColliders =
        debugRequest.forceColliders || frame.camera.debugMode == "colliders";
    debugRequest.bounds =
        debugRequest.bounds || frame.camera.debugMode == "bounds";
    if (!shadowPass && !appendDebugGeometry3D(world, primitives_, debugRequest)) {
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
    if (resolveSurface) {
      if (!postProcess_.present(frame, sourceTarget.color, frame.frameBuffer,
                                error))
        return false;
      ++overlayDrawCalls;
      overlayTriangles += 2;
    } else if (authoredOffscreen) {
      if (!overlay_.beginOverlayRegion(
              static_cast<std::uint16_t>(frame.viewId + 2U), frame.viewportX,
              frame.viewportY, frame.viewportWidth, frame.viewportHeight,
              deltaSeconds, error, frame.frameBuffer))
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
              deltaSeconds, error, frame.frameBuffer))
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
  statistics_.visibleMeshes = visibility.meshes.size() - distanceCulled;
  statistics_.culledMeshes = visibility.culled + distanceCulled;
  statistics_.mediumLodMeshes = mediumLodMeshes;
  statistics_.lowLodMeshes = lowLodMeshes;
  return true;
}

} // namespace demi::runtime::render
