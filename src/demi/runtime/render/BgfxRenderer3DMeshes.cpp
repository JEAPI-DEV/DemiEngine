#include <span>
#include "demi/runtime/destruction/DetachedFragmentFade3D.h"
#include "demi/runtime/geometry/MeshDeformation3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"
#include "demi/runtime/render/bgfx3d/MeshInstanceBatch3D.h"
#include "demi/runtime/render/bgfx3d/MeshTransform3D.h"
#include "demi/runtime/render/bgfx3d/PreparedMeshDraw3D.h"
#include "demi/runtime/render/bgfx3d/PrimitiveMeshFactory3D.h"
#include "demi/runtime/render/bgfx3d/VisibleMeshGeometry3D.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshInstances3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/model/MeshMaterialSections3D.h"
#include "demi/runtime/terrain/TerrainRenderLod.h"

#include <algorithm>
#include <map>

namespace demi::runtime::render {
namespace {
void hashValue(std::uint64_t &hash, const std::uint32_t value) {
  constexpr std::uint64_t Prime = 1099511628211ULL;
  for (unsigned shift = 0; shift < 32; shift += 8) {
    hash ^= (value >> shift) & 0xffU;
    hash *= Prime;
  }
}

std::uint64_t
meshCacheRevision(const MeshRendererComponent &mesh,
                  const MeshMaterialSections3D *sections = nullptr) {
  // Geometry mutation APIs advance revision. Keep this key constant-size so
  // resident procedural meshes do not rescan all vertex data every camera.
  std::uint64_t hash = 14695981039346656037ULL;
  hashValue(hash, static_cast<std::uint32_t>(mesh.revision));
  hashValue(hash, static_cast<std::uint32_t>(mesh.revision >> 32U));
  hashValue(hash, static_cast<std::uint32_t>(mesh.vertices.size()));
  hashValue(hash, static_cast<std::uint32_t>(mesh.normals.size()));
  hashValue(hash, static_cast<std::uint32_t>(mesh.uvs.size()));
  hashValue(hash, static_cast<std::uint32_t>(mesh.vertexColors.size()));
  for (const char value : mesh.shape) {
    hashValue(hash, static_cast<unsigned char>(value));
  }
  if (sections && sections->indices) {
    hashValue(hash, static_cast<std::uint32_t>(sections->indexRevision));
    hashValue(hash, static_cast<std::uint32_t>(sections->indexRevision >> 32U));
  }
  return hash;
}

} // namespace

const GpuMesh3D *BgfxRenderer3D::primitiveMesh(const std::string &shape,
                                               std::string &error) {
  auto &cached = primitiveMeshes_[shape];
  if (!cached) {
    cached = std::make_unique<CachedMesh>(resources_);
  }
  if (!cached->gpu.valid()) {
    PrimitiveMeshData3D primitive;
    if (!createPrimitiveMesh3D(shape, primitive)) {
      error = "unsupported primitive shape '" + shape + "'.";
      return nullptr;
    }
    if (!cached->gpu.upload(primitive.positions, primitive.textureCoordinates,
                            primitive.indices, 0xffffffffU, error)) {
      return nullptr;
    }
  }
  return &cached->gpu;
}

bool BgfxRenderer3D::submitVisibleMeshes(
    const SceneVisibility3D &visibility, const BgfxCameraFrame3D &frame,
    bool shadowPass, std::size_t firstTransparent,
    std::span<const DrawUniformValue> lightingUniforms,
    ProgramHandle defaultMeshProgram, ProgramHandle defaultInstancedProgram,
    ProgramHandle defaultSkinnedProgram, MeshSubmissionStatistics &statistics,
    std::string &error, int selectedSurfacePass,
    PreparedInstanceView3D *preparedInstances) {
  meshPbr_.setFrameTime(waterTime_);
  const std::size_t pbrFamily =
      defaultMeshProgram == analyticDepthProgram(directionalMeshProgram_,
                                                 frame.outputAnalyticDepth)
          ? 3
          : 0;
  const auto pbrMeshProgram =
      analyticDepthProgram(pbrPrograms_[pbrFamily], frame.outputAnalyticDepth);
  const auto pbrInstanceProgram = analyticDepthProgram(
      pbrPrograms_[pbrFamily + 1], frame.outputAnalyticDepth);
  const auto pbrSkinProgram = analyticDepthProgram(pbrPrograms_[pbrFamily + 2],
                                                   frame.outputAnalyticDepth);
  const MeshSurfacePassPrograms3D passPrograms{
      .pbrMesh = pbrMeshProgram,
      .pbrInstanced = pbrInstanceProgram,
      .depthOnly = shadowPass || (frame.pass == BgfxCameraPass3D::OpaqueDepth),
      .overdraw = frame.camera.debugMode == "overdraw"};
  auto *opaquePlan =
      preparedInstances ? &preparedInstances->opaquePlan() : nullptr;
  const bool replayOpaquePlan = opaquePlan && opaquePlan->ready() &&
                                opaquePlan->groups().groupCount() != 0;
  if (selectedSurfacePass != 1) {
    RuntimeProfiler::setGauge("Renderer3D.opaque_plan_replayed",
                              replayOpaquePlan ? 1.0 : 0.0);
    RuntimeProfiler::setGauge("Renderer3D.opaque_plan_groups",
                              opaquePlan ? opaquePlan->groups().groupCount()
                                         : 0);
    RuntimeProfiler::setGauge(
        "Renderer3D.opaque_plan_copies",
        opaquePlan ? opaquePlan->statistics().visibleCopies : 0);
  }

  const bool hasTransparentMeshes = firstTransparent < visibility.meshes.size();
  auto &bufferedDraws = statistics.draws;
  auto &bufferedTriangles = statistics.triangles;
  auto &distanceCulled = statistics.distanceCulled;
  auto &mediumLodMeshes = statistics.mediumLodMeshes;
  auto &lowLodMeshes = statistics.lowLodMeshes;
  MeshInstanceBatch3D instanceGroups;
  for (int surfacePass = 0; surfacePass < (hasTransparentMeshes ? 2 : 1);
       ++surfacePass) {
    if (selectedSurfacePass >= 0 && surfacePass != selectedSurfacePass) {
      continue;
    }
    const auto passBegin =
        visibility.meshes.begin() + (surfacePass == 0 ? 0 : firstTransparent);
    const auto passEnd =
        visibility.meshes.begin() +
        (surfacePass == 0 ? firstTransparent : visibility.meshes.size());
    // A source and selected model have one immutable resolved surface in this
    // pass. Cache the collector token, not duplicate Material/PBR ownership.
    using ModelSectionKey =
        std::pair<const std::string *, std::optional<std::size_t>>;
    std::unordered_map<const Entity *, std::map<ModelSectionKey, std::size_t>>
        preparedGroups;
    std::unordered_map<std::size_t, std::size_t> primitiveGroups;
    if (frame.updateContent) {
      ProfileScope collectScope("Renderer3D.submit_collect");
      for (auto visibleIt = passBegin; visibleIt != passEnd; ++visibleIt) {
        const VisibleMesh3D &visible = *visibleIt;
        const Entity &entity = *visible.entity;
        const auto *source = visible.preparedSource;
        if (surfacePass == 0 && opaquePlan && source && source->instances) {
          auto eligibility = opaquePlan->sourceEligibility(entity);
          if (!eligibility && !opaquePlan->ready()) {
            auto &raw = preparedInstances->source(entity);
            const bool eligible =
                raw.ready && opaqueInstanceSourceEligible(*source);
            if (eligible) {
              ProfileScope compileScope("Renderer3D.opaque_plan_compile");
              if (!compileOpaqueInstanceSource(*source, raw.visibility, frame,
                                               *opaquePlan, error)) {
                return false;
              }
            }
            opaquePlan->classifySource(entity, eligible);
            eligibility = eligible;
          }
          if (eligibility.value_or(false)) {
            continue;
          }
        }

        const auto *fade =
            source ? source->fade : entity.component<FragmentOpacity3D>();
        if (fade && shadowPass) {
          continue;
        }
        const auto *mesh =
            source ? source->mesh : entity.component<MeshRendererComponent>();
        const auto dents = source ? source->dents : entityMeshDents3D(entity);
        const WorldTransform3D &transform = visible.transform;
        const auto *player =
            source ? source->animation
                   : entity.component<AnimationPlayer3DComponent>();
        const ModelLodSelection lod = selectModelLod(
            *mesh, player, transform.position,
            frame.lodPosition.value_or(frame.position), !dents.empty(),
            staticShadowModelLodFloor3D(visible, frame, shadowPass));
        // Sections are draw ranges of one source copy, not additional models.
        // Extraction emits section zero once for each ordinary/instanced copy.
        const bool countSource =
            !visible.materialSection || *visible.materialSection == 0;
        if (lod.isCulled) {
          if (countSource) {
            ++distanceCulled;
          }
          continue;
        }
        const std::string &selectedModel = *lod.model;
        if (countSource) {
          mediumLodMeshes += lod.level == 1 ? 1U : 0U;
          lowLodMeshes += lod.level == 2 ? 1U : 0U;
        }
        std::array<float, 16> uncachedModelMatrix;
        const auto *modelMatrix = visible.preparedModelMatrix;
        if (!modelMatrix) {
          uncachedModelMatrix = composeMeshTransform3D(transform, mesh->size);
          modelMatrix = &uncachedModelMatrix;
        }
        if (source && !visible.materialSection && !visible.terrainSurface) {
          const auto primitive = primitiveSurfaces_.surfaceFor(*source);
          if (primitive) {
            const auto existing = primitiveGroups.find(*primitive);
            if (existing != primitiveGroups.end()) {
              instanceGroups.appendTransform(existing->second, *modelMatrix);
              continue;
            }
            const auto &surface = primitiveSurfaces_.surface(*primitive);
            const auto *gpu = primitiveMesh(surface.shape, error);
            if (!gpu) {
              error = entity.id + ": " + error;
              return false;
            }
            const auto texture =
                surface.texture.empty()
                    ? TextureView2D{}
                    : materialTexture(surface.texture, ImageMipSemantic::Color);
            if (!surface.texture.empty() && !texture.handle) {
              error = "Missing material colour texture: " + surface.texture;
              return false;
            }
            auto parameters = surface.metalRough;
            parameters[3] = texture.handle && !texture.srgbDecoded ? 1.F : 0.F;
            const auto group = instanceGroups.add(
                *gpu, texture.handle ? texture.handle : whiteTexture_,
                surface.tint, parameters, false, *modelMatrix);
            primitiveGroups.emplace(*primitive, group);
            continue;
          }
        }
        const ModelSectionKey surfaceKey{lod.model, visible.materialSection};
        const auto preparedSource = preparedGroups.find(&entity);
        if (preparedSource != preparedGroups.end()) {
          const auto preparedLod = preparedSource->second.find(surfaceKey);
          if (preparedLod != preparedSource->second.end()) {
            instanceGroups.appendTransform(preparedLod->second, *modelMatrix);
            continue;
          }
        }
        const auto *sections = visible.modelSections ? visible.modelSections
                               : visible.terrainSections
                                   ? visible.terrainSections
                               : source ? source->sections
                                        : visibleMeshSections3D(visible);
        const MeshMaterialSection3D *section = nullptr;
        if (visible.materialSection) {
          section = &sections->sections.at(*visible.materialSection);
        }
        const auto &materialId = section ? section->material : mesh->material;
        const MaterialBinding *material = materials_.find(materialId);
        if ((frame.pass == BgfxCameraPass3D::OpaqueDepth) && material && material->program) {
          error = "Water depth capture does not support custom material "
                  "programs: " +
                  materialId;
          return false;
        }
        const auto modelLighting = modelUnlit_.find(selectedModel);
        const bool modelUnlit =
            (modelLighting != modelUnlit_.end() && modelLighting->second) ||
            (material != nullptr && material->unlit);
        const bool pbr = usesMeshPbrSurface3D(material, *mesh);
        const auto meshProgram = pbr ? pbrMeshProgram : defaultMeshProgram;
        const auto skinProgram = pbr ? pbrSkinProgram : defaultSkinnedProgram;
        if (pbr && !meshProgram) {
          error = pbrProgramError_.empty()
                      ? "PBR shader is unavailable on this device"
                      : pbrProgramError_;
          return false;
        }
        float terrainTextureScale = 1;
        if (const auto *marker = source
                                     ? source->terrain
                                     : entity.component<TerrainRenderLod>()) {
          const auto field = marker->chunk ? marker->chunk->field : nullptr;
          if (field) {
            const auto biome =
                std::ranges::find(field->biomeIds, marker->biome);
            if (biome != field->biomeIds.end()) {
              terrainTextureScale = field->biomeTextureScale(
                  std::size_t(biome - field->biomeIds.begin()));
            }
          }
        }
        if (!meshPbr_.prepare(material, *mesh, whiteTexture_,
                              terrainTextureScale, error)) {
          return false;
        }
        PreparedMeshDraw3D prepared(
            *mesh, material,
            fade ? std::optional<float>(fade->value) : std::nullopt, modelUnlit,
            shadowPass || (frame.pass == BgfxCameraPass3D::OpaqueDepth),
            frame.camera.debugMode == "overdraw", meshProgram);
        const auto &surface = prepared.surface;
        const auto &state = prepared.state;
        const auto program = prepared.program;
        if (shadowPass && (surface.state.blend != BlendMode::Opaque ||
                           !surface.state.writeDepth)) {
          continue;
        }
        const auto &builtinSurfaceTint = prepared.builtinSurfaceTint;
        const auto &metalRough = prepared.metalRough;
        auto &drawUniforms = meshDrawUniforms_;
        const auto colorSemantic = material && material->program
                                       ? ImageMipSemantic::Linear
                                       : ImageMipSemantic::Color;
        const auto resolveColorTexture = [&](std::string_view id) {
          const auto texture = materialTexture(id, colorSemantic);
          prepared.metalRough[3] =
              texture.handle && !texture.srgbDecoded ? 1.F : 0.F;
          return texture;
        };
        const auto queueSurfaceInstances = [&](const GpuMesh3D &gpu,
                                               TextureHandle texture) {
          const bool sharedStaticInstances =
              (source ? source->instances != nullptr
                      : entity.hasComponent<MeshInstances3DComponent>()) &&
              !player && dents.empty() && !fade && !surface.transparent() &&
              !(material && material->program);
          if (!sharedStaticInstances) {
            return false;
          }
          const auto group = instanceGroups.addSurface(
              gpu, texture, *mesh, material, prepared, pbr, *modelMatrix,
              section && !visible.drawAllSections ? section->firstIndex : 0,
              section && !visible.drawAllSections ? section->indexCount
                                                  : UINT32_MAX);
          preparedGroups[&entity].emplace(surfaceKey, group);
          return true;
        };
        bool queued = false;
        if (const auto *relief =
                source ? source->relief
                       : entity.component<SurfaceRelief3DComponent>();
            relief && (!relief->heightMap.empty() || relief->tiles.x > 1 ||
                       relief->tiles.y > 1)) {
          if (player || !dents.empty() || !mesh->model.empty() ||
              !mesh->vertices.empty() || mesh->shape != "cube") {
            error = "SurfaceRelief3D requires an undeformed cube MeshRenderer";
            return false;
          }
          const auto *gpu = reliefMeshes_.get(*relief, mesh->size, error);
          if (!gpu) {
            return false;
          }
          std::string textureId = mesh->texture;
          if (textureId.empty() && material) {
            textureId = material->albedoTexture;
          }
          const auto texture = resolveColorTexture(textureId);
          if (!textureId.empty() && !texture.handle) {
            error = "Missing material colour texture: " + textureId;
            return false;
          }
          const auto resolved = texture.handle ? texture.handle : whiteTexture_;
          if (queueSurfaceInstances(*gpu, resolved)) {
            queued = true;
          } else if (!material && !surface.transparent() && !fade) {
            instanceGroups.add(*gpu, resolved, builtinSurfaceTint, metalRough,
                               false, *modelMatrix);
            queued = true;
          } else {
            prepared.fillUniforms(lightingUniforms, material, drawUniforms);
            queued = gpu->draw(meshPbr_, frame.viewId, program, resolved,
                               meshSampler_, *modelMatrix, state, error,
                               drawUniforms);
            if (queued) {
              ++bufferedDraws;
              bufferedTriangles += gpu->indexCount() / 3U;
            }
          }
        } else if (visible.terrainSurface) {
          const auto *gpu = visible.terrainGpuMesh;
          if (!gpu) {
            error = "Terrain mesh preparation did not complete: " + entity.id;
            return false;
          }
          std::string textureId = mesh->texture;
          if (textureId.empty() && material) {
            textureId = material->albedoTexture;
          }
          const auto texture = resolveColorTexture(textureId);
          if (!textureId.empty() && !texture.handle) {
            error = "Missing material colour texture: " + textureId;
            return false;
          }
          const bool drawSectionRange = section && !visible.drawAllSections;
          prepared.fillUniforms(lightingUniforms, material, drawUniforms);
          queued =
              gpu->draw(meshPbr_, frame.viewId, program,
                        texture.handle ? texture.handle : whiteTexture_,
                        meshSampler_, *modelMatrix, state, error, drawUniforms,
                        drawSectionRange ? section->firstIndex : 0,
                        drawSectionRange ? section->indexCount : UINT32_MAX);
          if (queued) {
            ++bufferedDraws;
            const auto drawnIndices =
                drawSectionRange ? section->indexCount : gpu->indexCount();
            bufferedTriangles += drawnIndices / 3U;
            if (!shadowPass) {
              terrainStatistics_.triangles += drawnIndices / 3U;
            }
          }
        } else if (!mesh->vertices.empty()) {
          const std::uint64_t signature = meshCacheRevision(*mesh, sections);
          std::span<const std::uint32_t> indices;
          if (sections && sections->indices) {
            indices = *sections->indices;
          }
          auto &cached = dynamicMeshes_[entity.id];
          if (!cached) {
            cached = std::make_unique<CachedMesh>(resources_);
          }
          if (!cached->animationModel.empty()) {
            cached->gpu.clear();
            cached->animationModel.clear();
            cached->skinPalette.clear();
            cached->inlineColorSignature = 0;
          }
          if (cached->inlineColorSignature != signature) {
            if (!mesh->validateVertexColors(error)) {
              error = entity.id + ": " + error;
              return false;
            }
            cached->inlineColors.resize(mesh->vertexColors.size());
            std::ranges::transform(mesh->vertexColors,
                                   cached->inlineColors.begin(),
                                   packVertexColorRgba8);
            cached->inlineColorSignature = signature;
          }
          if (dents.empty() &&
              (cached->signature != signature || !cached->gpu.valid())) {
            if (!cached->gpu.upload(mesh->vertices, mesh->uvs, indices,
                                    0xffffffffU, error, mesh->normals,
                                    cached->inlineColors)) {
              error = entity.id + ": " + error;
              return false;
            }
            cached->signature = signature;
          }
          std::string textureId = mesh->texture;
          if (textureId.empty() && material != nullptr) {
            textureId = material->albedoTexture;
          }
          const TextureView2D texture = resolveColorTexture(textureId);
          if (!textureId.empty() && !texture.handle) {
            error = "Missing material colour texture: " + textureId;
            return false;
          }
          const GpuMesh3D *drawMesh = &cached->gpu;
          if (!dents.empty()) {
            drawMesh = deformedMeshes_.get(entity.id, "inline", signature,
                                           mesh->vertices, mesh->uvs, indices,
                                           cached->inlineColors, dents, error);
            if (!drawMesh) {
              return false;
            }
          }
          const bool drawSectionRange = section && !visible.drawAllSections;
          const auto resolvedTexture =
              texture.handle ? texture.handle : whiteTexture_;
          if (queueSurfaceInstances(*drawMesh, resolvedTexture)) {
            queued = true;
          } else {
            prepared.fillUniforms(lightingUniforms, material, drawUniforms);
            queued = drawMesh->draw(
                meshPbr_, frame.viewId, program, resolvedTexture, meshSampler_,
                *modelMatrix, state, error, drawUniforms,
                drawSectionRange ? section->firstIndex : 0,
                drawSectionRange ? section->indexCount : UINT32_MAX);
            if (queued) {
              ++bufferedDraws;
              const auto drawnIndices = drawSectionRange
                                            ? section->indexCount
                                            : drawMesh->indexCount();
              bufferedTriangles += drawnIndices / 3U;
            }
          }
        } else if (!selectedModel.empty()) {
          const auto cached = modelMeshes_.find(selectedModel);
          if (cached == modelMeshes_.end()) {
            error =
                "No migrated GPU model is loaded for " + selectedModel + ".";
            return false;
          }
          const CachedMesh *drawMesh = cached->second.get();
          const GpuSkinnedMesh3D *gpuSkin = nullptr;
          const std::vector<float> *skinPalette = nullptr;
          if (player != nullptr) {
            const auto animated = dynamicMeshes_.find(entity.id);
            if (animated == dynamicMeshes_.end()) {
              error =
                  entity.id + ": Animated mesh preparation did not complete.";
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
            drawGpu = deformedMeshes_.get(
                entity.id, selectedModel, 0, rest.positions, rest.uvs,
                rest.indices, rest.colors, dents, error);
            if (!drawGpu) {
              return false;
            }
          }
          const bool drawSectionRange = section && !visible.drawAllSections;
          const auto firstIndex = drawSectionRange ? section->firstIndex : 0;
          const auto indexCount =
              drawSectionRange ? section->indexCount : UINT32_MAX;
          // Match primitive meshes: an entity texture overrides material and
          // embedded model albedo. Keep it in the instancing key below as well.
          std::string textureId = mesh->texture;
          if (textureId.empty() && material != nullptr) {
            textureId = material->albedoTexture;
          }
          const TextureView2D texture = resolveColorTexture(textureId);
          if (!textureId.empty() && !texture.handle) {
            error = "Missing material colour texture: " + textureId;
            return false;
          }
          const TextureHandle resolvedTexture =
              texture.handle ? texture.handle : whiteTexture_;
          if (gpuSkin) {
            if (!skinProgram) {
              error = "PBR skinned shader is unavailable on this device";
              return false;
            }
            // GPU skinning uses the built-in lit program even when a material
            // also provides a custom program for static geometry.
            prepared.fillUniforms(lightingUniforms, material, drawUniforms);
            drawUniforms.front().values = builtinSurfaceTint;
            drawUniforms.push_back({.handle = skinMatricesUniform_,
                                    .values = *skinPalette,
                                    .count = static_cast<std::uint16_t>(
                                        skinPalette->size() / 16)});
            drawUniforms.push_back(
                {.handle = skinImportUniform_,
                 .values = animatedModels_.at(selectedModel).importTransform});
            queued = gpuSkin->draw(meshPbr_, frame.viewId, skinProgram,
                                   resolvedTexture, meshSampler_, *modelMatrix,
                                   state, drawUniforms, error, firstIndex,
                                   indexCount);
            if (queued) {
              ++bufferedDraws;
              bufferedTriangles +=
                  (drawSectionRange ? indexCount : gpuSkin->indexCount()) / 3;
            }
          } else if (queueSurfaceInstances(*drawGpu, resolvedTexture)) {
            queued = true;
          } else if (player == nullptr && material == nullptr &&
                     dents.empty() && !surface.transparent() && !fade) {
            instanceGroups.add(*drawGpu, resolvedTexture, builtinSurfaceTint,
                               metalRough, modelUnlit, *modelMatrix);
            queued = true;
          } else {
            prepared.fillUniforms(lightingUniforms, material, drawUniforms);
            queued =
                drawGpu->draw(meshPbr_, frame.viewId, program, resolvedTexture,
                              meshSampler_, *modelMatrix, state, error,
                              drawUniforms, firstIndex, indexCount);
            if (queued) {
              ++bufferedDraws;
              bufferedTriangles +=
                  (drawSectionRange ? indexCount : drawGpu->indexCount()) / 3U;
            }
          }
        } else {
          const auto *gpu = primitiveMesh(mesh->shape, error);
          if (!gpu) {
            error = entity.id + ": " + error;
            return false;
          }
          std::string textureId = mesh->texture;
          if (textureId.empty() && material != nullptr) {
            textureId = material->albedoTexture;
          }
          const TextureView2D texture = resolveColorTexture(textureId);
          if (!textureId.empty() && !texture.handle) {
            error = "Missing material colour texture: " + textureId;
            return false;
          }
          const TextureHandle resolvedTexture =
              texture.handle ? texture.handle : whiteTexture_;
          if (queueSurfaceInstances(*gpu, resolvedTexture)) {
            queued = true;
          } else if (material == nullptr && !surface.transparent() && !fade) {
            instanceGroups.add(*gpu, resolvedTexture, builtinSurfaceTint,
                               metalRough, false, *modelMatrix);
            queued = true;
          } else {
            prepared.fillUniforms(lightingUniforms, material, drawUniforms);
            queued = gpu->draw(meshPbr_, frame.viewId, program, resolvedTexture,
                               meshSampler_, *modelMatrix, state, error,
                               drawUniforms);
            if (queued) {
              ++bufferedDraws;
              bufferedTriangles += gpu->indexCount() / 3U;
            }
          }
        }
        if (!queued) {
          error = "Could not queue 3D geometry for entity " + entity.id + ".";
          return false;
        }
      }
    }
    if (frame.updateContent && surfacePass == 0) {
      ProfileScope flushScope("Renderer3D.submit_flush");
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
      if (!instanceGroups.flush(commands_, frame.viewId, defaultMeshProgram,
                                defaultInstancedProgram, meshSampler_, state,
                                lightingUniforms, bufferedDraws,
                                bufferedTriangles, error, &meshPbr_,
                                whiteTexture_, passPrograms)) {
        return false;
      }
      if (opaquePlan) {
        if (!opaquePlan->groups().flush(
                commands_, frame.viewId, defaultMeshProgram,
                defaultInstancedProgram, meshSampler_, state, lightingUniforms,
                bufferedDraws, bufferedTriangles, error, &meshPbr_,
                whiteTexture_, passPrograms)) {
          return false;
        }
        const auto &planned = opaquePlan->statistics();
        distanceCulled += planned.distanceCulled;
        mediumLodMeshes += planned.mediumLodMeshes;
        lowLodMeshes += planned.lowLodMeshes;
        opaquePlan->finish();
        RuntimeProfiler::setGauge("Renderer3D.opaque_plan_replayed",
                                  replayOpaquePlan ? 1.0 : 0.0);
        RuntimeProfiler::setGauge("Renderer3D.opaque_plan_groups",
                                  opaquePlan->groups().groupCount());
        RuntimeProfiler::setGauge("Renderer3D.opaque_plan_copies",
                                  planned.visibleCopies);
      }
    }
  }
  return true;
}

} // namespace demi::runtime::render
