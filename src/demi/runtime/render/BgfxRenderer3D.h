#pragma once
#include <span>
#include "demi/runtime/destruction/CosmeticDebris3D.h"
#include <stop_token>

#include "demi/assets/AssetRegistry.h"
#include "demi/assets/GltfSkinnedModel.h"
#include "demi/runtime/concurrency/JobSystem.h"
#include "demi/runtime/render/BgfxRenderer2D.h"
#include "demi/runtime/render/MaterialLibrary.h"
#include "demi/runtime/render/ParticleSimulation3D.h"
#include "demi/runtime/render/RenderStatistics.h"
#include "demi/runtime/render/backend/TextureLibrary2D.h"
#include "demi/runtime/render/bgfx3d/BgfxCameraFrame3D.h"
#include "demi/runtime/render/bgfx3d/DeformedMeshCache3D.h"
#include "demi/runtime/render/bgfx3d/DirectionalShadow3D.h"
#include "demi/runtime/render/bgfx3d/GpuMesh3D.h"
#include "demi/runtime/render/bgfx3d/GpuSkinnedMesh3D.h"
#include "demi/runtime/render/bgfx3d/MeshInstancePreparation3D.h"
#include "demi/runtime/render/bgfx3d/MeshPbrBindings3D.h"
#include "demi/runtime/render/bgfx3d/MeshResourceRetention3D.h"
#include "demi/runtime/render/bgfx3d/OpaqueSceneCapture3D.h"
#include "demi/runtime/render/bgfx3d/ParticleBillboardRenderer3D.h"
#include "demi/runtime/render/bgfx3d/PostProcessRenderer3D.h"
#include "demi/runtime/render/bgfx3d/PreparedInstanceView3D.h"
#include "demi/runtime/render/bgfx3d/PrimitiveCanvas3D.h"
#include "demi/runtime/render/bgfx3d/PrimitiveSurfacePreparation3D.h"
#include "demi/runtime/render/bgfx3d/ReliefMeshCache3D.h"
#include "demi/runtime/render/bgfx3d/SceneLightBuffer3D.h"
#include "demi/runtime/render/bgfx3d/SceneVisibility3D.h"
#include "demi/runtime/render/bgfx3d/ModelLodSampling3D.h"
#include "demi/runtime/render/bgfx3d/SkyRenderer3D.h"
#include "demi/runtime/render/bgfx3d/TerrainCameraDemand3D.h"
#include "demi/runtime/render/bgfx3d/TerrainDrawSelection3D.h"
#include "demi/runtime/render/bgfx3d/TerrainGpuFieldCache3D.h"
#include "demi/runtime/render/bgfx3d/TerrainGpuMeshCache3D.h"
#include "demi/runtime/render/bgfx3d/TerrainMaterialResidency3D.h"
#include "demi/runtime/render/bgfx3d/TerrainPatchBatch3D.h"
#include "demi/runtime/render/bgfx3d/UnderwaterCamera3D.h"
#include "demi/runtime/render/bgfx3d/VisualAnimationBudget3D.h"
#include "demi/runtime/scene/EntityComponentQuery.h"
#include "demi/runtime/scene/model/World.h"
#include "demi/runtime/terrain/TerrainLodResidency.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace demi::runtime {
struct MeshRendererComponent;
struct AnimationPlayer3DComponent;
} // namespace demi::runtime

namespace demi::runtime::render {

struct TerrainRenderStatistics3D {
  std::array<std::size_t, 4> selectedSurfaces{};
  std::size_t triangles = 0;
  std::uint64_t uploads = 0;
  std::size_t residentMeshes = 0;
  std::size_t residentBytes = 0;
  std::size_t cpuResidentBytes = 0;
  std::size_t cpuPendingBytes = 0;
  std::size_t cpuPendingJobs = 0;
  std::size_t cpuReadyLevels = 0;
  std::size_t gpuPatchDraws = 0;
  std::size_t gpuPatchInstances = 0;
  std::size_t gpuPatchShadowDraws = 0;
  std::size_t gpuPatchShadowInstances = 0;
  std::size_t gpuPatchFallbacks = 0;
  std::array<std::size_t, 4> gpuSelectedLevels{};
  std::string gpuFallbackReason;
};

// Runtime-facing composition root for bgfx 3D rendering. Specialized mesh,
// particle, post-process, camera, and overlay systems retain their own narrow
// ownership boundaries.
class BgfxRenderer3D {
public:
  BgfxRenderer3D(GpuResources &resources, RenderCommands &commands,
                 bool enableGpuSkinning = true);
  ~BgfxRenderer3D();

  BgfxRenderer3D(const BgfxRenderer3D &) = delete;
  BgfxRenderer3D &operator=(const BgfxRenderer3D &) = delete;

  [[nodiscard]] bool initialize(std::string &error);
  void shutdown();
  [[nodiscard]] bool loadAssets(const AssetRegistry &registry,
                                std::vector<std::string> &diagnostics);
  [[nodiscard]] bool
  beginAssetPreparation(const AssetRegistry &registry, JobSystem &jobs,
                        std::vector<std::string> &diagnostics,
                        bool prepareMaterialImages = true);
  [[nodiscard]] bool
  stepAssetPreparation(double budgetMilliseconds, bool &complete,
                       std::vector<std::string> &diagnostics);
  void cancelAssetPreparation();
  [[nodiscard]] double assetPreparationProgress() const;
  [[nodiscard]] bool renderFrame(const World &world,
                                 const BgfxCameraFrame3D &frame,
                                 float deltaSeconds, std::string &error);

  // Resource-only preparation before the live world's next presentation. Opens
  // a shared presentation scope which the host must close after rendering its
  // current world; this keeps incoming terrain demand alive during loading UI.
  [[nodiscard]] bool prepareScenePresentation(const World &world,
                                              const BgfxCameraFrame3D &frame,
                                              bool &complete,
                                              std::string &error);
  void cancelScenePresentationPreparation();

  // Hosts with multiple cameras bracket them once; direct/editor rendering
  // automatically creates a single-camera presentation scope.
  void beginPresentation();
  void endPresentation(bool succeeded);
  void tickInactiveTerrain();
  [[nodiscard]] std::string takeTerrainDiagnostic() {
    return std::exchange(terrainDiagnostic_, {});
  }
  [[nodiscard]] bool terrainPreparationPending() const {
    return terrainLodResidency_.hasPendingWork() ||
           terrainGpuFields_.hasPendingWork() ||
           terrainMaterials_.hasPendingWork();
  }
  [[nodiscard]] const TerrainRenderStatistics3D &terrainStatistics() const {
    return terrainStatistics_;
  }

  [[nodiscard]] const RenderStatistics &statistics() const {
    return statistics_;
  }
  [[nodiscard]] double lastExtractionMilliseconds() const {
    return lastExtractionMilliseconds_;
  }

private:
  void configureTerrainPresentation(const World &world,
                                    const BgfxCameraFrame3D &frame,
                                    const SceneLighting3D &lighting);
  struct AssetPreparation;
  std::shared_ptr<AssetPreparation> assetPreparation_;
  struct TextureVariantPreparation;
  std::shared_ptr<TextureVariantPreparation> textureVariantPreparation_;
  [[nodiscard]] bool prepareTextureVariant(std::string_view id,
                                           ImageMipSemantic semantic,
                                           bool &complete, std::string &error);
  void cancelTextureVariantPreparation();
  std::set<std::pair<std::string, ImageMipSemantic>> preparedTextureVariants_;
  bool drawCosmeticDebris(std::vector<CosmeticFragment3D> &,
                          const BgfxCameraFrame3D &,
                          std::span<const DrawUniformValue>, ProgramHandle,
                          std::uint32_t &, std::uint32_t &, std::string &);
  bool renderView(const World &, const BgfxCameraFrame3D &, float,
                  std::string &, bool shadowPass, const SceneLighting3D &,
                  const BgfxCameraFrame3D &lodCamera,
                  OpaqueSceneCapture3D::Target *waterCapture = nullptr,
                  PreparedInstanceView3D *preparedInstances = nullptr);
  bool prepareAnalyticDepthPrograms(std::string &error);
  [[nodiscard]] ProgramHandle analyticDepthProgram(ProgramHandle program,
                                                   bool enabled) const;
  std::vector<std::pair<ProgramHandle, ProgramHandle>> analyticDepthPrograms_;
  OpaqueSceneCapture3D::Target *prepareWaterCapture(const World &,
                                                    const BgfxCameraFrame3D &,
                                                    const SceneLighting3D &,
                                                    std::string &error);
  bool prepareTerrainMeshes(SceneVisibility3D &, std::string &error);
  struct MeshSubmissionStatistics {
    std::uint32_t draws = 0;
    std::uint32_t triangles = 0;
    std::uint32_t distanceCulled = 0;
    std::uint32_t mediumLodMeshes = 0;
    std::uint32_t lowLodMeshes = 0;
  };
  bool renderWaterAndTransparent(const SceneVisibility3D &, BgfxCameraFrame3D &,
                                 const RenderTargetHandles &,
                                 OpaqueSceneCapture3D::Target &,
                                 const SceneLighting3D &,
                                 std::span<const DrawUniformValue> ,
                                 std::size_t firstTransparent,
                                 MeshSubmissionStatistics &,
                                 std::string &error);
  const GpuMesh3D *primitiveMesh(const std::string &shape, std::string &error);
  bool submitVisibleMeshes(const SceneVisibility3D &, const BgfxCameraFrame3D &,
                           bool shadowPass, std::size_t firstTransparent,
                           std::span<const DrawUniformValue> ,
                           ProgramHandle defaultMeshProgram,
                           ProgramHandle defaultInstancedProgram,
                           ProgramHandle defaultSkinnedProgram,
                           MeshSubmissionStatistics &, std::string &error,
                           int selectedSurfacePass = -1,
                           PreparedInstanceView3D *preparedInstances = nullptr);
  [[nodiscard]] bool opaqueInstanceSourceEligible(
      const MeshInstancePreparation3D::EntitySource &source) const;
  [[nodiscard]] bool compileOpaqueInstanceSource(
      const MeshInstancePreparation3D::EntitySource &source,
      const SceneVisibility3D &copies, const BgfxCameraFrame3D &frame,
      OpaqueInstancePlan3D &plan, std::string &error);
  using ModelLodSelection = ModelLodGeometrySelection3D;
  [[nodiscard]] ModelLodSelection
  selectModelLod(const MeshRendererComponent &mesh,
                 const AnimationPlayer3DComponent *player, Vec3 position,
                 Vec3 camera, bool preserveHigh,
                 float shadowDistanceFloor = 0) const;
  struct RenderTarget {
    RenderTargetHandles handles;
    std::uint16_t width = 1;
    std::uint16_t height = 1;
    bool depth = true;
    int requestedSamples = 1;
  };
  bool setRenderTargetSamples(RenderTarget &, const std::string &id,
                              int samples, std::string &error);

  struct CachedMesh {
    explicit CachedMesh(GpuResources &resources) : gpu(resources) {}
    GpuMesh3D gpu;
    std::uint64_t signature = 0;
    std::uint64_t inlineColorSignature = 0;
    std::vector<std::uint32_t> inlineColors;
    MeshGeometry3D restGeometry;
    std::shared_ptr<const MeshMaterialSections3D> materialSections;
    std::optional<MeshBounds3D> bounds;
    std::string animationModel;
    std::unique_ptr<GpuSkinnedMesh3D> gpuSkin;
    std::vector<float> skinPalette;
    VisualAnimationSample3D animationSample;
  };

  [[nodiscard]] bool
  prepareAnimatedMeshes(std::span<const VisibleMesh3D> visible,
                        const BgfxCameraFrame3D &frame, std::string &error);

  GpuResources &resources_;
  DirectionalShadow3D shadows_;
  SceneLightBuffer3D lightBuffer_;
  RenderCommands &commands_;
  PrimitiveCanvas3D primitives_;
  PostProcessRenderer3D postProcess_;
  OpaqueSceneCapture3D opaqueCapture_;
  WaterSurfaceRenderer3D waterRenderer_;
  UnderwaterCamera3D underwaterCamera_;
  float waterTime_ = 0.F;
  bool waterTimeAdvanced_ = false;
  ParticleBillboardRenderer3D particleRenderer_;
  BgfxRenderer2D overlay_;
  TextureLibrary2D textures_;
  struct TextureSource {
    std::filesystem::path path;
    // Imported images are recreated from source with their channel/factor
    // conversion whenever a sampling variant is demanded again.
    std::function<bool(ImageMipSemantic, PreparedTexture2D &, std::string &,
                       std::stop_token)>
        prepareImported;
    TextureSampling2D sampling;
    std::array<bool, 3> failedVariants{};
    std::array<std::string, 3> variantErrors{};
  };
  std::unordered_map<std::string, TextureSource> textureSources_;
  SkyRenderer3D sky_;
  MaterialLibrary materials_;
  MeshPbrBindings3D meshPbr_;
  std::array<ProgramHandle, 6> pbrPrograms_{};
  std::string pbrProgramError_;
  TextureView2D materialTexture(std::string_view id, ImageMipSemantic semantic);
  void finishMaterialTexturePresentation(bool succeeded);
  std::unordered_map<std::string, std::array<std::uint64_t, 3>, StringHash,
                     std::equal_to<>>
      materialTextureUse_;
  std::uint64_t texturePresentationSerial_ = 0;
  std::uint64_t successfulTexturePresentation_ = 0;
  ProgramHandle meshProgram_;
  ProgramHandle directionalMeshProgram_, directionalInstancedProgram_,
      directionalSkinnedProgram_;
  bool gpuSkinningRequested_ = true;
  bool gpuSkinningEnabled_ = false;
  ProgramHandle skinnedMeshProgram_;
  UniformHandle skinMatricesUniform_, skinImportUniform_;
  ProgramHandle instancedMeshProgram_;
  SamplerHandle meshSampler_;
  UniformHandle tintUniform_;
  UniformHandle alphaCutoffUniform_;
  UniformHandle debugModeUniform_;
  MeshInstancePreparation3D instancePreparation_;
  PreparedInstanceView3D cameraInstances_;
  std::array<PreparedInstanceView3D, 4> shadowInstances_;
  EntityComponentQuery entityQueries_;
  PrimitiveSurfacePreparation3D primitiveSurfaces_;
  UniformHandle viewPositionUniform_;
  UniformHandle metalRoughUniform_;
  UniformHandle lightDirectionUniform_;
  UniformHandle lightColorUniform_;
  UniformHandle ambientColorUniform_;
  UniformHandle fogColorUniform_;
  UniformHandle fogRangeUniform_;
  TextureHandle whiteTexture_;
  std::unordered_map<std::string, std::unique_ptr<CachedMesh>> dynamicMeshes_;
  std::unordered_map<std::string, std::unique_ptr<CachedMesh>> primitiveMeshes_;
  std::unordered_map<std::string, std::unique_ptr<CachedMesh>> modelMeshes_;
  DeformedMeshCache3D deformedMeshes_;
  MeshResourceRetention3D meshRetention_;
  JobSystem extractionJobs_;
  TerrainDrawSelection3D terrainSelection_;
  TerrainGpuMeshCache3D terrainMeshes_;
  TerrainLodResidency terrainLodResidency_;
  TerrainCameraDemand3D terrainCameraDemand_;
  TerrainGpuFieldCache3D terrainGpuFields_;
  TerrainMaterialResidency3D terrainMaterials_;
  TerrainPatchBatch3D terrainPatches_;
  std::string terrainGpuCapabilityReason_;
  std::string reportedTerrainGpuFallback_;
  ProgramHandle terrainProgram_;
  ProgramHandle directionalTerrainProgram_;
  ProgramHandle mappedTerrainProgram_;
  ProgramHandle directionalMappedTerrainProgram_;
  ProgramHandle opaqueMappedTerrainProgram_;
  ProgramHandle directionalOpaqueMappedTerrainProgram_;
  TerrainRenderStatistics3D terrainStatistics_;
  std::size_t reportedTerrainFailures_ = 0;
  std::string terrainDiagnostic_;
  std::uint64_t terrainUploadsAtPresentationStart_ = 0;
  bool presentationActive_ = false;
  // Command adapters consume spans synchronously. Descriptors may be reused
  // after submit returns; their value spans remain local to the current draw.
  std::vector<DrawUniformValue> meshDrawUniforms_;
  ReliefMeshCache3D reliefMeshes_;
  std::unordered_map<std::string, assets::GltfSkinnedModel3D> animatedModels_;
  std::unordered_map<std::string, bool> modelUnlit_;
  std::unordered_map<std::string, RenderTarget> renderTargets_;
  ParticleSimulation3D particles_;
  RenderStatistics statistics_;
  double lastExtractionMilliseconds_ = 0.0;
  bool initialized_ = false;
};

} // namespace demi::runtime::render
