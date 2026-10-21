#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/destruction/DestructionWorld3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/bgfx3d/MeshShadowSections3D.h"
#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/terrain/TerrainWaterMesh.h"

#include "demi/runtime/render/bgfx3d/DebugGeometry3D.h"

#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"
#include "demi/runtime/render/bgfx3d/MeshDrawOrder3D.h"
#include "demi/runtime/render/bgfx3d/MeshSurface3D.h"
#include "demi/runtime/render/bgfx3d/MeshTransform3D.h"
#include "demi/runtime/render/bgfx3d/SceneLighting3D.h"
#include "demi/runtime/render/bgfx3d/WorldTextProjection3D.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ParticleEmitter3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/PostProcessStackComponent.h"

#include <algorithm>
#include <cstdlib>

namespace demi::runtime::render {
namespace {

Point3D point(const Vec3 value) {
  return {.x = value.x, .y = value.y, .z = value.z};
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
    : resources_(resources), shadows_(resources, commands),
      lightBuffer_(resources, shadows_), commands_(lightBuffer_),
      primitives_(resources, commands_), postProcess_(resources, commands_),
      opaqueCapture_(resources), waterRenderer_(resources, commands),
      particleRenderer_(resources, commands_), overlay_(resources, commands_),
      textures_(resources), sky_(resources), materials_(resources),
      meshPbr_(resources, commands_,
               [this](std::string_view id, ImageMipSemantic semantic) {
                 return materialTexture(id, semantic);
               }),
      gpuSkinningRequested_(enableGpuSkinning), deformedMeshes_(resources),
      terrainMeshes_(resources), terrainLodResidency_(extractionJobs_),
      terrainGpuFields_(resources, extractionJobs_),
      terrainMaterials_(resources, extractionJobs_), terrainPatches_(resources),
      reliefMeshes_(resources) {}

BgfxRenderer3D::~BgfxRenderer3D() { shutdown(); }

bool BgfxRenderer3D::initialize(std::string &error) {
  if (initialized_)
    return true;
  if (!shadows_.initialize(error))
    return false;
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
  directionalMeshProgram_ =
      resources_.createBuiltinProgram(BuiltinProgram::Directional3D, error);
  directionalInstancedProgram_ = resources_.createBuiltinProgram(
      BuiltinProgram::Directional3DInstanced, error);
  pbrProgramError_.clear();
  if (resources_.limits().maxTextureSamplers >= 9) {
    constexpr std::array programs{BuiltinProgram::Pbr3D,
                                  BuiltinProgram::Pbr3DInstanced,
                                  BuiltinProgram::Pbr3DSkinned,
                                  BuiltinProgram::Pbr3DDirectional,
                                  BuiltinProgram::Pbr3DDirectionalInstanced,
                                  BuiltinProgram::Pbr3DDirectionalSkinned};
    for (std::size_t index = 0; index < programs.size(); ++index) {
      pbrPrograms_[index] =
          resources_.createBuiltinProgram(programs[index], pbrProgramError_);
    }
  } else {
    pbrProgramError_ =
        "PBR materials require nine texture samplers on this device";
  }
  meshSampler_ = resources_.createSampler("s_texColor", error);
  tintUniform_ =
      resources_.createUniform("u_tint", UniformType::Vec4, 1, error);
  alphaCutoffUniform_ =
      resources_.createUniform("u_alphaCutoff", UniformType::Vec4, 1, error);
  debugModeUniform_ =
      resources_.createUniform("u_debugMode", UniformType::Vec4, 1, error);
  viewPositionUniform_ =
      resources_.createUniform("u_viewPosition", UniformType::Vec4, 1, error);
  metalRoughUniform_ =
      resources_.createUniform("u_metalRough", UniformType::Vec4, 1, error);
  lightDirectionUniform_ =
      resources_.createUniform("u_lightDirection", UniformType::Vec4, 1, error);
  lightColorUniform_ =
      resources_.createUniform("u_lightColor", UniformType::Vec4, 1, error);
  ambientColorUniform_ =
      resources_.createUniform("u_ambientColor", UniformType::Vec4, 1, error);
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
      !directionalInstancedProgram_ || !meshSampler_ || !tintUniform_ ||
      !alphaCutoffUniform_ || !debugModeUniform_ || !viewPositionUniform_ ||
      !metalRoughUniform_ || !whiteTexture_ || !lightDirectionUniform_ ||
      !lightColorUniform_ || !ambientColorUniform_) {
    shutdown();
    return false;
  }
  const char *gpuOverride = std::getenv("DEMI_GPU_SKINNING");
  gpuSkinningEnabled_ =
      gpuSkinningRequested_ &&
      (!gpuOverride || std::string_view(gpuOverride) != "0") &&
      (resources_.shaderBackend() == "vulkan" ||
       resources_.shaderBackend() == "noop");
  if (gpuSkinningEnabled_) {
    skinnedMeshProgram_ =
        resources_.createBuiltinProgram(BuiltinProgram::Lit3DSkinned, error);
    directionalSkinnedProgram_ = resources_.createBuiltinProgram(
        BuiltinProgram::Directional3DSkinned, error);
    skinMatricesUniform_ = resources_.createUniform(
        "u_skinMatrices", UniformType::Matrix4, MaximumGpuSkinMatrices, error);
    skinImportUniform_ = resources_.createUniform(
        "u_skinImport", UniformType::Matrix4, 1, error);
    if (!skinnedMeshProgram_ || !directionalSkinnedProgram_ ||
        !skinMatricesUniform_ || !skinImportUniform_) {
      shutdown();
      return false;
    }
  }
  const auto limits = resources_.limits();
  const auto heightCaps = resources_.textureCapabilities(TextureFormat::R32F);
  const auto normalCaps =
      resources_.textureCapabilities(TextureFormat::RGBA32F);
  const auto idCaps = resources_.textureCapabilities(TextureFormat::RGBA8);
  const bool terrainCapable =
      limits.supportsInstancing && limits.maxTextureSamplers >= 7 &&
      heightCaps.sampled2D && heightCaps.vertexSampling &&
      normalCaps.sampled2D && normalCaps.vertexSampling && idCaps.sampled2D;
  if (!terrainCapable) {
    terrainGpuCapabilityReason_ =
        "GPU terrain capabilities: instancing=" +
        std::to_string(limits.supportsInstancing) +
        ", samplers=" + std::to_string(limits.maxTextureSamplers) +
        ", R32F sampled/vertex=" + std::to_string(heightCaps.sampled2D) + "/" +
        std::to_string(heightCaps.vertexSampling) +
        ", RGBA32F sampled/vertex=" + std::to_string(normalCaps.sampled2D) +
        "/" + std::to_string(normalCaps.vertexSampling) +
        ", RGBA8 sampled=" + std::to_string(idCaps.sampled2D);
  }
  if (terrainCapable) {
    std::string terrainError;
    terrainProgram_ = resources_.createBuiltinProgram(BuiltinProgram::Terrain3D,
                                                      terrainError);
    directionalTerrainProgram_ = resources_.createBuiltinProgram(
        BuiltinProgram::Terrain3DDirectional, terrainError);
    if (!terrainProgram_ || !directionalTerrainProgram_) {
      terrainGpuCapabilityReason_ =
          "GPU terrain shader unavailable: " + terrainError;
      if (terrainProgram_) {
        resources_.destroy(terrainProgram_);
      }
      if (directionalTerrainProgram_) {
        resources_.destroy(directionalTerrainProgram_);
      }
      terrainProgram_ = {};
      directionalTerrainProgram_ = {};
    }
  }
  if (terrainCapable && limits.maxTextureSamplers >= 12 &&
      idCaps.sampledArray) {
    std::string mappedError;
    mappedTerrainProgram_ = resources_.createBuiltinProgram(
        BuiltinProgram::TerrainMapped3D, mappedError);
    directionalMappedTerrainProgram_ = resources_.createBuiltinProgram(
        BuiltinProgram::TerrainMapped3DDirectional, mappedError);
    opaqueMappedTerrainProgram_ = resources_.createBuiltinProgram(
        BuiltinProgram::TerrainMappedOpaque3D, mappedError);
    directionalOpaqueMappedTerrainProgram_ = resources_.createBuiltinProgram(
        BuiltinProgram::TerrainMappedOpaque3DDirectional, mappedError);
  }
  initialized_ = true;
  return true;
}

void BgfxRenderer3D::shutdown() {
  cancelAssetPreparation();
  meshPbr_.shutdown();
  for (auto &program : pbrPrograms_) {
    if (program) {
      resources_.destroy(program);
    }
    program = {};
  }
  lightBuffer_.shutdown();
  shadows_.shutdown();
  sky_.clear();
  opaqueCapture_.clear();
  waterRenderer_.clear();
  for (const auto program :
       {directionalMeshProgram_, directionalInstancedProgram_,
        directionalSkinnedProgram_})
    if (program)
      resources_.destroy(program);
  directionalMeshProgram_ = {};
  directionalInstancedProgram_ = {};
  directionalSkinnedProgram_ = {};
  if (skinnedMeshProgram_)
    resources_.destroy(skinnedMeshProgram_);
  if (skinMatricesUniform_)
    resources_.destroy(skinMatricesUniform_);
  if (skinImportUniform_)
    resources_.destroy(skinImportUniform_);
  skinnedMeshProgram_ = {};
  skinMatricesUniform_ = {};
  skinImportUniform_ = {};
  deformedMeshes_.clear();
  terrainPatches_.clear();
  terrainGpuFields_.clear();
  terrainMaterials_.clear();
  for (auto *program :
       {&mappedTerrainProgram_, &directionalMappedTerrainProgram_,
        &opaqueMappedTerrainProgram_,
        &directionalOpaqueMappedTerrainProgram_}) {
    if (*program) {
      resources_.destroy(*program);
      *program = {};
    }
  }
  if (terrainProgram_) {
    resources_.destroy(terrainProgram_);
  }
  if (directionalTerrainProgram_) {
    resources_.destroy(directionalTerrainProgram_);
  }
  terrainProgram_ = {};
  directionalTerrainProgram_ = {};
  terrainLodResidency_.clear();
  reportedTerrainFailures_ = terrainLodResidency_.statistics().failedJobs;
  terrainDiagnostic_.clear();
  terrainCameraDemand_.clear();
  terrainMeshes_.clear();
  terrainSelection_.clear();
  terrainStatistics_ = {};
  presentationActive_ = false;
  reliefMeshes_.clear();
  dynamicMeshes_.clear();
  meshRetention_.clear();
  meshDrawUniforms_.clear();
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
  textureSources_.clear();
  materialTextureUse_.clear();
  texturePresentationSerial_ = 0;
  successfulTexturePresentation_ = 0;
  if (whiteTexture_)
    resources_.destroy(whiteTexture_);
  if (meshSampler_)
    resources_.destroy(meshSampler_);
  for (const UniformHandle uniform :
       {tintUniform_, alphaCutoffUniform_, debugModeUniform_,
        viewPositionUniform_, metalRoughUniform_, lightDirectionUniform_,
        lightColorUniform_, ambientColorUniform_})
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
  viewPositionUniform_ = {};
  metalRoughUniform_ = {};
  lightDirectionUniform_ = {};
  lightColorUniform_ = {};
  ambientColorUniform_ = {};
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
  const auto endView = std::uint32_t(frame.viewId) + CameraViewCount3D;
  if (endView > resources_.limits().maxViews || endView > UINT16_MAX) {
    error = "The camera pass range exceeds the renderer view limit.";
    return false;
  }
  const bool ownsPresentation = !presentationActive_;
  if (ownsPresentation) {
    beginPresentation();
  }
  bool presentationSucceeded = false;
  struct PresentationScope {
    BgfxRenderer3D &renderer;
    bool ownsPresentation;
    bool &succeeded;
    ~PresentationScope() {
      if (ownsPresentation) {
        renderer.endPresentation(succeeded);
      }
    }
  } presentationScope{*this, ownsPresentation, presentationSucceeded};
  if (frame.updateContent) {
    meshRetention_.collect(instancePreparation_.worldSources(world, &extractionJobs_));
  }
  const auto sceneLighting =
      collectSceneLighting3D(world, frame.camera.renderMask, &entityQueries_);
  configureTerrainPresentation(world, frame, sceneLighting);
  const auto &lighting =
      frame.lightingOverride ? *frame.lightingOverride : sceneLighting;
  if (!shadows_.prepare(lighting, frame, error)) {
    return false;
  }
  // Advance shared visual time before any shadow/depth/color view consumes
  // it. The presentation guard keeps multiple cameras on the same wind phase.
  if (!waterTimeAdvanced_) {
    waterTime_ += std::max(deltaSeconds, 0.F);
    waterTimeAdvanced_ = true;
  }
  auto mainFrame = frame;
  std::size_t shadowBatches = 0;
  std::size_t shadowTriangles = 0;
  const auto depthFrames = shadows_.depthFrames();
  for (std::size_t cascade = 0; cascade < depthFrames.size(); ++cascade) {
    ProfileScope scope("Renderer3D.directional_shadow");
    shadows_.cast(cascade);
    if (!renderView(world, depthFrames[cascade], 0, error, true, sceneLighting,
                    frame)) {
      return false;
    }
    shadowBatches += statistics_.batches;
    shadowTriangles += statistics_.triangles;
    const auto prefix = "Renderer3D.shadow." + std::to_string(cascade) + ".";
    RuntimeProfiler::setGauge(prefix + "triangles", statistics_.triangles);
    RuntimeProfiler::setGauge(prefix + "batches", statistics_.batches);
    ++mainFrame.viewId;
  }
  RuntimeProfiler::setGauge("Renderer3D.shadow_batches", shadowBatches);
  RuntimeProfiler::setGauge("Renderer3D.shadow_triangles", shadowTriangles);
  RuntimeProfiler::setGauge("Renderer3D.shadow_cascades", depthFrames.size());
  shadows_.receive();
  if (frame.updateContent && frame.camera.debugMode == "shaded" &&
      resources_.shaderBackend() != "noop") {
    mainFrame.underwater = underwaterCamera_.sample(world, frame.position);
  } else {
    mainFrame.underwater.reset();
  }
  std::string captureError;
  auto *capture = prepareWaterCapture(world, mainFrame, captureError);
  if (!captureError.empty()) {
    error = std::move(captureError);
    return false;
  }
  if (capture != nullptr) {
    auto depthFrame = mainFrame;
    depthFrame.captureLinearDepth = true;
    depthFrame.underwater.reset();
    depthFrame.frameBuffer = capture->depth.frameBuffer;
    depthFrame.viewportX = 0;
    depthFrame.viewportY = 0;
    depthFrame.viewportWidth = capture->width;
    depthFrame.viewportHeight = capture->height;
    depthFrame.destinationSamples = 1;
    depthFrame.camera.renderTarget.clear();
    depthFrame.camera.debugMode = "shaded";
    depthFrame.camera.renderScale = 1.F;
    depthFrame.camera.renderHud = false;
    depthFrame.camera.clearColor = {1.F, 1.F, 1.F, 1.F};
    depthFrame.camera.clearMode = "color";
    depthFrame.postProcess.reset();
    if (!renderView(world, depthFrame, 0.F, error, false, sceneLighting,
                    frame)) {
      return false;
    }
    ++mainFrame.viewId;
  }
  if (mainFrame.underwater && capture) {
    mainFrame.underwater->linearDepth = capture->depth.color;
  }
  const bool rendered = renderView(world, mainFrame, deltaSeconds, error, false,
                                   sceneLighting, frame, capture);
  statistics_.shadowPasses = depthFrames.size();
  if (rendered && frame.updateContent) {
    deformedMeshes_.retain([this](const std::string &id) {
      return meshRetention_.containsDeformed(id);
    });
    std::erase_if(dynamicMeshes_, [this](const auto &entry) {
      return !meshRetention_.containsDynamic(entry.first);
    });
  }
  presentationSucceeded = rendered;
  return rendered;
}

bool BgfxRenderer3D::renderView(const World &world,
                                const BgfxCameraFrame3D &inputFrame,
                                float deltaSeconds, std::string &error,
                                bool shadowPass,
                                const SceneLighting3D &sceneLighting,
                                const BgfxCameraFrame3D &lodCamera,
                                OpaqueSceneCapture3D::Target *waterCapture) {
  auto frame = inputFrame;
  const bool depthCapture = frame.captureLinearDepth;
  if (!initialized_) {
    error = "BgfxRenderer3D must be initialized before rendering.";
    return false;
  }
  const int msaaSamples =
      (shadowPass || depthCapture) ? 1 : std::max(sceneLighting.msaaSamples, 1);
  if (!shadowPass && !depthCapture) {
    postProcess_.setMsaaSamples(msaaSamples);
  }
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
  const bool needsTargetSampleUpdate = authoredOffscreen && frame.updateContent;
  if (needsTargetSampleUpdate &&
      !setRenderTargetSamples(renderTarget->second, frame.camera.renderTarget,
                              msaaSamples, error)) {
    return false;
  }
  const bool hostOffscreen = static_cast<bool>(frame.frameBuffer);
  const bool applyPostProcess =
      hasPostProcessEffects(frame.postProcess) || frame.underwater.has_value();
  const bool targetPostProcess =
      authoredOffscreen && frame.updateContent && applyPostProcess;
  const float renderScale =
      authoredOffscreen ? 1.0F
                        : std::clamp(frame.camera.renderScale, 0.25F, 2.0F);
  // Scaling needs an intermediate surface even without color effects. Drawing
  // a smaller view directly into the destination leaves its edges untouched.
  const bool needsMsaaResolve =
      !authoredOffscreen && msaaSamples != frame.destinationSamples;
  // An unchanged authored target already contains its last completed image.
  // Reprocessing a shared scratch surface here could sample another camera.
  const bool resolveSurface =
      !depthCapture &&
      (authoredOffscreen ? targetPostProcess
                         : (applyPostProcess || renderScale != 1.0F ||
                            needsMsaaResolve || waterCapture != nullptr));
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
  sourceTarget.msaaSamples = frame.destinationSamples;
  if (authoredOffscreen && !targetPostProcess) {
    sourceTarget = renderTarget->second.handles;
  } else if (resolveSurface) {
    sourceTarget = postProcess_.scratchTarget(
        std::max<std::uint16_t>(renderWidth, 1),
        std::max<std::uint16_t>(renderHeight, 1), error, msaaSamples);
  } else if (hostOffscreen) {
    sourceTarget.frameBuffer = frame.frameBuffer;
  }
  const bool offscreen = authoredOffscreen || hostOffscreen || resolveSurface;
  if (!shadowPass) {
    RuntimeProfiler::setGauge("Renderer3D.msaa_requested", msaaSamples);
    RuntimeProfiler::setGauge("Renderer3D.msaa_backend_request",
                              sourceTarget.msaaSamples);
  }
  if (offscreen &&
      (!sourceTarget.frameBuffer || (resolveSurface && !sourceTarget.color))) {
    if (error.empty()) {
      error = "Could not create the camera's offscreen render surface.";
    }
    return false;
  }
  terrainPatches_.beginView();
  if (frame.updateContent) {
    terrainPatches_.prepareExtraction(
        world, frame, lodCamera, terrainSelection_, terrainGpuFields_,
        materials_, terrainCameraDemand_, sceneLighting.terrainGpuCacheBytes,
        bool(terrainProgram_ && directionalTerrainProgram_), &terrainMaterials_,
        sceneLighting.terrainDetailDistance,
        bool(mappedTerrainProgram_ && directionalMappedTerrainProgram_));
  }
  SceneVisibility3D visibility;
  const bool isMainContentView =
      frame.updateContent && !shadowPass && !depthCapture;
  auto cosmeticFragments = isMainContentView && world.destruction3D
                               ? world.destruction3D->cosmeticFragments()
                               : std::vector<CosmeticFragment3D>{};
  if (frame.updateContent) {
    ProfileScope scope("Renderer3D.extract_view");
    const auto extractionStarted = std::chrono::steady_clock::now();
    visibility = extractVisibleMeshes3D(
        world, frame, &extractionJobs_, &terrainSelection_, &lodCamera,
        &terrainLodResidency_, &terrainCameraDemand_, &terrainPatches_, &materials_,
        [this](std::string_view model) -> std::optional<MeshBounds3D> {
          const auto cached = modelMeshes_.find(std::string(model));
          return cached == modelMeshes_.end() ? std::nullopt : cached->second->bounds;
        }, &instancePreparation_);
    lastExtractionMilliseconds_ =
        std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - extractionStarted)
            .count();
  } else {
    lastExtractionMilliseconds_ = 0.0;
  }
  if (frame.updateContent) {
    ProfileScope scope("Renderer3D.prepare_terrain_meshes");
    if (!prepareTerrainMeshes(visibility, error)) {
      return false;
    }
  }
  if (shadowPass) {
    ProfileScope scope("Renderer3D.compact_shadow_sections");
    compactOpaqueShadowSections3D(visibility.meshes, materials_);
  }
  if (frame.updateContent && instancePreparation_.hasAnimations(world) &&
      !prepareAnimatedMeshes(visibility.meshes, frame, error)) {
    return false;
  }
  std::size_t firstTransparent = visibility.meshes.size();
  if (!shadowPass) {
    ProfileScope scope("Renderer3D.order_surfaces");
    firstTransparent = orderMeshSurfaces3D(visibility.meshes, materials_,
                                          frame.position, frame.forward);
  }
  if (depthCapture) {
    visibility.meshes.resize(firstTransparent);
  }
  const bool hasTransparentMeshes = firstTransparent < visibility.meshes.size();
  if (frame.updateContent) {
    const bool usesTargetLocalCoordinates = offscreen && !shadowPass;
    const View3DConfig viewConfig{
        .id = frame.viewId,
        .x = usesTargetLocalCoordinates ? std::uint16_t{0} : frame.viewportX,
        .y = usesTargetLocalCoordinates ? std::uint16_t{0} : frame.viewportY,
        .width = std::max<std::uint16_t>(renderWidth, 1),
        .height = std::max<std::uint16_t>(renderHeight, 1),
        .clearRgba = packClearColorRgba8(frame.camera.clearColor),
        .eye = point(frame.position),
        .forward = point(frame.forward),
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
        .sequential = !cosmeticFragments.empty() || hasTransparentMeshes,
        .debugName =
            shadowPass ? "Shadow" : (depthCapture ? "OpaqueDepth" : "Main"),
    };
    if (!primitives_.begin(viewConfig, error)) {
      return false;
    }
  }

  // Invalid intermediate authoring data must not poison subsequent frames.
  struct PrimitiveFrameScope {
    PrimitiveCanvas3D &canvas;
    ~PrimitiveFrameScope() { canvas.cancel(); }
  } primitiveFrame{primitives_};

  reliefMeshes_.setRetentionBudget(sceneLighting.reliefCacheMeshes,
                                   sceneLighting.reliefImageCacheBytes);
  bool skyDrawn = false;
  const bool shouldDrawSky =
      !depthCapture && frame.updateContent && frame.camera.perspective &&
      frame.camera.clearMode == "color" && !sceneLighting.skyTexture.empty();
  if (shouldDrawSky) {
    const auto texture =
        materialTexture(sceneLighting.skyTexture, ImageMipSemantic::Linear);
    if (!texture.handle) {
      error =
          "Environment sky texture is not loaded: " + sceneLighting.skyTexture;
      return false;
    }
    if (!sky_.draw(commands_, frame.viewId, texture.handle, frame.position,
                   error)) {
      return false;
    }
    skyDrawn = true;
  }
  const SceneLighting3D &lighting =
      frame.lightingOverride ? *frame.lightingOverride : sceneLighting;
  if (!lightBuffer_.prepare(frame.viewId, lighting, error)) {
    return false;
  }
  RuntimeProfiler::setGauge("Renderer3D.additional_lights",
                            double(lighting.lights.size()));
  const bool directionalOnly = !lighting.hasAdditionalLights();
  RuntimeProfiler::setGauge("Renderer3D.directional_shader",
                            directionalOnly ? 1.0 : 0.0);
  const ProgramHandle defaultMeshProgram =
      directionalOnly ? directionalMeshProgram_ : meshProgram_;
  const ProgramHandle defaultInstancedProgram =
      directionalOnly ? directionalInstancedProgram_ : instancedMeshProgram_;
  const ProgramHandle defaultSkinnedProgram =
      directionalOnly ? directionalSkinnedProgram_ : skinnedMeshProgram_;
  const std::array<float, 4> whiteTint{1.0F, 1.0F, 1.0F, 1.0F};
  const std::array<float, 4> noAlphaCutoff{};
  const std::array<float, 4> debugMode =
      depthCapture ? std::array<float, 4>{8.F, frame.forward.x, frame.forward.y,
                                          frame.forward.z}
                   : std::array<float, 4>{
                         debugModeValue(frame.camera.debugMode), 0.F, 0.F, 0.F};
  const std::array<float, 4> viewPosition{
      frame.position.x, frame.position.y, frame.position.z,
      depthCapture ? 1.F / frame.camera.farClip : 0.F};
  const std::array<float, 4> defaultMetalRough{0.0F, 0.8F, 0.0F, 0.0F};
  const std::array<DrawUniformValue, 8> lightingUniforms{{
      {.handle = tintUniform_, .values = whiteTint},
      {.handle = alphaCutoffUniform_, .values = noAlphaCutoff},
      {.handle = debugModeUniform_, .values = debugMode},
      {.handle = lightDirectionUniform_, .values = lighting.direction},
      {.handle = lightColorUniform_, .values = lighting.directionalColor},
      {.handle = ambientColorUniform_, .values = lighting.ambient},
      {.handle = viewPositionUniform_, .values = viewPosition},
      {.handle = metalRoughUniform_, .values = defaultMetalRough},
  }};
  if (frame.updateContent) {
    const auto terrainProgram =
        directionalOnly ? directionalTerrainProgram_ : terrainProgram_;
    auto fallback = terrainPatches_.flush(
        commands_, frame.viewId, terrainProgram, lightingUniforms,
        frame.camera.debugMode == "overdraw", error,
        directionalOnly ? directionalMappedTerrainProgram_
                        : mappedTerrainProgram_,
        directionalOnly ? directionalOpaqueMappedTerrainProgram_
                        : opaqueMappedTerrainProgram_);
    if (!fallback.empty()) {
      visibility.meshes.insert(visibility.meshes.end(), fallback.begin(),
                               fallback.end());
      {
        ProfileScope scope("Renderer3D.prepare_terrain_meshes");
        if (!prepareTerrainMeshes(visibility, error)) {
          return false;
        }
      }
      if (shadowPass) {
        ProfileScope scope("Renderer3D.compact_shadow_sections");
        compactOpaqueShadowSections3D(visibility.meshes, materials_);
        firstTransparent = visibility.meshes.size();
      } else {
        ProfileScope scope("Renderer3D.order_surfaces");
        firstTransparent = orderMeshSurfaces3D(visibility.meshes, materials_,
                                              frame.position, frame.forward);
      }
    }
  }
  const auto &patches = terrainPatches_.statistics();
  if (depthCapture) {
    RuntimeProfiler::setGauge("Renderer3D.water_depth_triangles",
                              patches.triangles);
  } else if (shadowPass) {
    terrainStatistics_.gpuPatchShadowDraws += patches.draws;
    terrainStatistics_.gpuPatchShadowInstances += patches.instances;
  } else {
    terrainStatistics_.gpuPatchDraws += patches.draws;
    terrainStatistics_.gpuPatchInstances += patches.instances;
    terrainStatistics_.gpuPatchFallbacks += patches.fallbackPatches;
    terrainStatistics_.triangles += patches.triangles;
    for (std::size_t level = 0; level < patches.selectedLevels.size();
         ++level) {
      terrainStatistics_.gpuSelectedLevels[level] +=
          patches.selectedLevels[level];
    }
    terrainStatistics_.gpuFallbackReason = patches.fallbackReason;
    if (patches.fallbackReason ==
            "GPU terrain programs/capabilities are unavailable" &&
        !terrainGpuCapabilityReason_.empty()) {
      terrainStatistics_.gpuFallbackReason = terrainGpuCapabilityReason_;
    }
    for (const auto &draw : visibility.meshes) {
      if (draw.terrainSurface &&
          (!draw.materialSection || *draw.materialSection == 0)) {
        ++terrainStatistics_.selectedSurfaces.at(draw.terrainLevel);
      }
    }
  }
  reliefMeshes_.beginFrame();
  MeshSubmissionStatistics meshStatistics;
  {
    ProfileScope scope("Renderer3D.submit_meshes");
    if (!submitVisibleMeshes(visibility, frame, shadowPass, firstTransparent,
                             lightingUniforms, defaultMeshProgram,
                             defaultInstancedProgram, defaultSkinnedProgram,
                             meshStatistics, error, waterCapture ? 0 : -1)) {
      return false;
    }
  }
  if (waterCapture != nullptr &&
      !renderWaterAndTransparent(visibility, frame, sourceTarget, *waterCapture,
                                 sceneLighting, lightingUniforms,
                                 firstTransparent, meshStatistics, error)) {
    return false;
  }
  std::uint32_t bufferedDraws =
      meshStatistics.draws + patches.draws + (skyDrawn ? 1U : 0U);
  std::uint32_t bufferedTriangles =
      meshStatistics.triangles + patches.triangles + (skyDrawn ? 12U : 0U);
  const std::uint32_t distanceCulled = meshStatistics.distanceCulled;
  const std::uint32_t mediumLodMeshes = meshStatistics.mediumLodMeshes;
  const std::uint32_t lowLodMeshes = meshStatistics.lowLodMeshes;
  if (!cosmeticFragments.empty() &&
      !drawCosmeticDebris(cosmeticFragments, frame, lightingUniforms,
                          defaultMeshProgram, bufferedDraws, bufferedTriangles,
                          error)) {
    return false;
  }
  if (!shadowPass) {
    RuntimeProfiler::setGauge("Renderer3D.cosmetic_fragments",
                              cosmeticFragments.size());
  }
  if (isMainContentView) {
    particles_.update(world, deltaSeconds);
  }
  const auto particleData = isMainContentView
                                ? particles_.renderData(frame.camera.renderMask)
                                : std::vector<ParticleRenderData3D>{};
  std::vector<ParticleBillboardDraw3D> particleDraws;
  particleDraws.reserve(particleData.size());
  for (const ParticleRenderData3D &particle : particleData) {
    std::string textureId = particle.texture;
    const MaterialBinding *material = materials_.find(particle.material);
    if (textureId.empty() && material != nullptr) {
      textureId = material->albedoTexture;
    }
    const TextureView2D texture =
        materialTexture(textureId, ImageMipSemantic::Linear);
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
        if (left.particle.sortingOrder != right.particle.sortingOrder) {
          return left.particle.sortingOrder < right.particle.sortingOrder;
        }
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
    DebugGeometry3DRequest debugRequest = frame.debugGeometry;
    debugRequest.forceColliders =
        debugRequest.forceColliders || frame.camera.debugMode == "colliders";
    debugRequest.bounds =
        debugRequest.bounds || frame.camera.debugMode == "bounds";
    if (!shadowPass && !depthCapture &&
        !appendDebugGeometry3D(world, primitives_, debugRequest, &entityQueries_)) {
      error = "3D debug geometry exceeded the transient line capacity.";
      return false;
    }
    if (!primitives_.flush(error)) {
      return false;
    }
  }
  if (!particleRenderer_.draw(frame.viewId, frame, particleDraws, error)) {
    return false;
  }

  std::uint32_t overlayDrawCalls = 0;
  std::uint32_t overlayTriangles = 0;
  if (frame.updateContent && offscreen && frame.camera.renderHud &&
      frame.camera.renderHudToTarget && !frame.underwater &&
      !targetPostProcess) {
    if (!overlay_.beginOverlayRegion(
            static_cast<std::uint16_t>(frame.viewId + 1U), 0, 0, renderWidth,
            renderHeight, deltaSeconds, error, sourceTarget.frameBuffer)) {
      return false;
    }
    BgfxCameraFrame3D targetFrame = frame;
    targetFrame.viewportWidth = renderWidth;
    targetFrame.viewportHeight = renderHeight;
    const bool rendered =
        overlay_.drawHud(world) &&
        overlay_.drawUi(projectWorldText3D(world, targetFrame, &entityQueries_));
    if (!rendered || !overlay_.endFrame(error)) {
      if (error.empty()) {
        error = "Could not queue the render-target HUD.";
      }
      return false;
    }
    overlayDrawCalls += overlay_.statistics().drawCalls;
    overlayTriangles += overlay_.statistics().triangles;
  }

  if (offscreen && !depthCapture) {
    if (resolveSurface) {
      auto postFrame = frame;
      auto destination = frame.frameBuffer;
      std::optional<std::uint16_t> postView;
      if (targetPostProcess) {
        // Finish the authored target before its HUD and camera presentation.
        postView = static_cast<std::uint16_t>(frame.viewId + 1U);
        postFrame.viewportX = 0;
        postFrame.viewportY = 0;
        postFrame.viewportWidth = renderWidth;
        postFrame.viewportHeight = renderHeight;
        destination = renderTarget->second.handles.frameBuffer;
      }
      if (!postProcess_.present(postFrame, sourceTarget.color, destination,
                                error, postView)) {
        return false;
      }
      ++overlayDrawCalls;
      overlayTriangles += 2;
    }
    if (authoredOffscreen && targetPostProcess && frame.camera.renderHud &&
        frame.camera.renderHudToTarget) {
      if (!overlay_.beginOverlayRegion(
              frame.viewId + 2, 0, 0, renderWidth, renderHeight, deltaSeconds,
              error, renderTarget->second.handles.frameBuffer)) {
        return false;
      }
      auto hudFrame = frame;
      hudFrame.viewportWidth = renderWidth;
      hudFrame.viewportHeight = renderHeight;
      if (!overlay_.drawHud(world) ||
          !overlay_.drawUi(projectWorldText3D(world, hudFrame, &entityQueries_)) ||
          !overlay_.endFrame(error)) {
        return false;
      }
      overlayDrawCalls += overlay_.statistics().drawCalls;
      overlayTriangles += overlay_.statistics().triangles;
    }
    if (authoredOffscreen) {
      if (!overlay_.beginOverlayRegion(
              static_cast<std::uint16_t>(frame.viewId +
                                         (targetPostProcess ? 3U : 2U)),
              frame.viewportX, frame.viewportY, frame.viewportWidth,
              frame.viewportHeight, deltaSeconds, error, frame.frameBuffer)) {
        return false;
      }
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
        if (error.empty()) {
          error = "Could not present a 3D render target.";
        }
        return false;
      }
      overlayDrawCalls += overlay_.statistics().drawCalls;
      overlayTriangles += overlay_.statistics().triangles;
    }
    const bool hudAfterPresentation = !frame.camera.renderHudToTarget ||
                                      (frame.underwater && !authoredOffscreen);
    if (frame.camera.renderHud && hudAfterPresentation) {
      if (!overlay_.beginOverlayRegion(
              static_cast<std::uint16_t>(frame.viewId +
                                         (targetPostProcess ? 4U : 3U)),
              frame.viewportX, frame.viewportY, frame.viewportWidth,
              frame.viewportHeight, deltaSeconds, error, frame.frameBuffer)) {
        return false;
      }
      const bool rendered = overlay_.drawHud(world) &&
                            overlay_.drawUi(projectWorldText3D(world, frame, &entityQueries_));
      if (!rendered || !overlay_.endFrame(error)) {
        if (error.empty()) {
          error = "Could not render the camera HUD.";
        }
        return false;
      }
      overlayDrawCalls += overlay_.statistics().drawCalls;
      overlayTriangles += overlay_.statistics().triangles;
    }
  } else if (frame.camera.renderHud) {
    if (!overlay_.beginOverlayRegion(
            static_cast<std::uint16_t>(frame.viewId + 1U), frame.viewportX,
            frame.viewportY, frame.viewportWidth, frame.viewportHeight,
            deltaSeconds, error)) {
      return false;
    }
    const bool hudRendered = !frame.camera.renderHud || overlay_.drawHud(world);
    const bool worldTextRendered =
        !frame.camera.renderHud ||
        overlay_.drawUi(projectWorldText3D(world, frame, &entityQueries_));
    const bool hudFlushed = overlay_.endFrame(error);
    if (!hudRendered || !worldTextRendered) {
      error = "Could not queue the 3D scene overlay.";
      return false;
    }
    if (!hudFlushed) {
      return false;
    }
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
  statistics_.consideredMeshes =
      visibility.considered + patches.consideredPatches;
  statistics_.visibleMeshes =
      visibility.meshes.size() - distanceCulled + patches.instances;
  statistics_.culledMeshes =
      visibility.culled + distanceCulled + patches.culledPatches;
  statistics_.mediumLodMeshes = mediumLodMeshes;
  statistics_.lowLodMeshes = lowLodMeshes;
  return true;
}

} // namespace demi::runtime::render
