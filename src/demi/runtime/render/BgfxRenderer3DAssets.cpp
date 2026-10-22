#include "demi/runtime/render/BgfxRenderer3D.h"

#include "demi/assets/ModelImportProfile.h"
#include "demi/assets/RenderAsset.h"
#include "demi/runtime/diagnostics/DeviceLog.h"
#include "demi/runtime/render/backend/RenderAssetLoading.h"
#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"
#include "demi/runtime/render/bgfx3d/ImportedModelMaterials3D.h"
#include "demi/runtime/scene/model/MeshMaterialSections3D.h"

#include <nlohmann/json.hpp>
#include <stop_token>
#include <thread>

namespace demi::runtime::render {

namespace {

struct PreparedModelAsset {
  std::optional<assets::ModelImportProfile> profile;
  std::optional<assets::GltfSkinnedModel3D> animated;
  MeshGeometry3D geometry;
  std::vector<GpuSkinnedVertex3D> skinVertices;
  GpuSkinPaletteLayout skinLayout;
  std::vector<Vec3> normals;
  std::vector<PreparedImportedMaterial3D> materials;
  std::vector<std::string> diagnostics;
  std::string error;
};

void prepareModelAsset(const AssetManifest &asset, PreparedModelAsset &result,
                       std::stop_token stop) {
  if (stop.stop_requested()) {
    return;
  }
  const auto settings =
      nlohmann::json::parse(asset.settingsJson, nullptr, false);
  const bool hasProfile =
      settings.is_object() && settings.contains("model_import");
  result.profile =
      hasProfile
          ? assets::parseModelImportProfile(settings)
          : std::make_optional(assets::modelImportPreset("animated_character"));
  if (!result.profile) {
    result.error = "Invalid model import profile.";
    return;
  }
  auto &geometry = result.geometry;
  result.animated = assets::loadGltfSkinnedModel3D(
      asset.sourcePath, *result.profile, result.error);
  if (!result.animated) {
    return;
  }
  if (!result.animated->bindPosePositions(geometry.positions, result.error) ||
      !result.animated->bindPoseNormals(result.normals, result.error)) {
    return;
  }
  for (const auto &vertex : result.animated->vertices) {
    geometry.uvs.push_back(vertex.uv);
    geometry.colors.push_back(packVertexColorRgba8(vertex.color));
  }
  geometry.indices = result.animated->indices;
  std::string unsupportedSkin;
  if (!buildGpuSkinVertices(*result.animated, result.skinVertices,
                            result.skinLayout, unsupportedSkin)) {
    result.skinVertices.clear();
  }
  std::set<std::size_t> usedMaterials;
  for (const auto &section : result.animated->materialSections) {
    usedMaterials.insert(section.material);
  }
  for (const auto index : usedMaterials) {
    for (const auto &diagnostic :
         result.animated->materials.at(index).diagnostics) {
      result.diagnostics.push_back(diagnostic);
    }
    PreparedImportedMaterial3D material;
    if (!prepareImportedMaterial(asset, index,
                                 result.animated->materials.at(index),
                                 result.profile->materialPolicy == "unlit",
                                 material, result.error, stop)) {
      return;
    }
    result.materials.push_back(std::move(material));
  }
  // Animated geometry retains material identity, not duplicate encoded images.
  for (auto &material : result.animated->materials) {
    for (auto &[role, texture] : material.textures) {
      texture.encoded.reset();
    }
  }
}

template <typename Source>
bool prepareTextureSource(const Source &source, ImageMipSemantic semantic,
                          PreparedTexture2D &output, std::string &error,
                          std::stop_token stop = {}) {
  if (source.prepareImported) {
    return source.prepareImported(semantic, output, error, stop);
  }
  const auto encoded = readRenderAssetBytes(source.path);
  if (!error.empty()) {
    return false;
  }
  auto sampling = source.sampling;
  sampling.mipSemantic = semantic;
  return prepareTexture2D(encoded, sampling, output, error, stop);
}

ImageMipSemantic materialMapSemantic(assets::MaterialMapSlot slot) {
  using Slot = assets::MaterialMapSlot;
  if (slot == Slot::Albedo || slot == Slot::Emissive) {
    return ImageMipSemantic::Color;
  }
  if (slot == Slot::Normal || slot == Slot::DetailNormal) {
    return ImageMipSemantic::Normal;
  }
  return ImageMipSemantic::Linear;
}

} // namespace

struct BgfxRenderer3D::AssetPreparation {
  struct TextureRequest {
    std::string id;
    ImageMipSemantic semantic;
  };
  struct TextureResult {
    PreparedTexture2D texture;
    std::string error;
  };
  AssetRegistry registry;
  JobSystem *jobs = nullptr;
  std::size_t nextAsset = 0;
  std::vector<TextureRequest> textures;
  std::size_t nextTexture = 0;
  std::shared_ptr<PreparedModelAsset> model;
  std::shared_ptr<TextureResult> image;
  JobHandle job;
  std::stop_source cancellation;
  std::size_t completedUnits = 0;
  std::size_t totalUnits = 0;
};

struct BgfxRenderer3D::TextureVariantPreparation {
  struct Result {
    PreparedTexture2D texture;
    std::string error;
  };
  std::string id;
  ImageMipSemantic semantic;
  std::shared_ptr<Result> result;
  JobHandle job;
  std::stop_source cancellation;
};

TextureView2D BgfxRenderer3D::materialTexture(std::string_view id,
                                              ImageMipSemantic semantic) {
  if (!id.empty()) {
    auto use = materialTextureUse_.find(id);
    if (use == materialTextureUse_.end()) {
      use = materialTextureUse_.try_emplace(std::string(id)).first;
    }
    use->second[static_cast<std::size_t>(semantic)] =
        texturePresentationSerial_;
  }
  const auto texture = textures_.find(id, semantic);
  if (texture.handle) {
    return texture;
  }
  const auto target = renderTargets_.find(std::string(id));
  if (target != renderTargets_.end()) {
    // Render targets are live GPU data, not decoded images. The material slot
    // still defines interpretation: colour slots expect sRGB, data slots
    // linear.
    return {target->second.handles.color, std::uint16_t(target->second.width),
            std::uint16_t(target->second.height)};
  }
  const auto source = textureSources_.find(std::string(id));
  if (source == textureSources_.end()) {
    return {};
  }
  auto &descriptor = source->second;
  const auto variant = static_cast<std::size_t>(semantic);
  if (descriptor.failedVariants[variant]) {
    return {};
  }
  // Retain only source identity and sampling, never decoded image memory.
  // A second role pays its decode/upload once at first use.
  std::string error;
  PreparedTexture2D prepared;
  if (!prepareTextureSource(descriptor, semantic, prepared, error) ||
      !textures_.uploadPrepared(std::string(id), prepared, error)) {
    descriptor.failedVariants[variant] = true;
    descriptor.variantErrors[variant] =
        error.empty() ? "Could not read texture source." : error;
    deviceLogError(deviceLogMessage(
        "material.texture",
        std::string(id) + ": " +
            (error.empty() ? "could not read texture source" : error)));
    return {};
  }
  return textures_.find(id, semantic);
}

void BgfxRenderer3D::finishMaterialTexturePresentation(bool succeeded) {
  if (!succeeded) {
    return;
  }
  textures_.retainVariants(
      [this](std::string_view id, ImageMipSemantic semantic) {
        if (preparedTextureVariants_.contains({std::string(id), semantic})) {
          return true;
        }
        const auto use = materialTextureUse_.find(id);
        if (use == materialTextureUse_.end()) {
          return false;
        }
        const auto lastUse = use->second[static_cast<std::size_t>(semantic)];
        return lastUse != 0 && (lastUse == texturePresentationSerial_ ||
                                lastUse == successfulTexturePresentation_);
      });
  successfulTexturePresentation_ = texturePresentationSerial_;
}

bool BgfxRenderer3D::setRenderTargetSamples(RenderTarget &target,
                                            const std::string &id, int samples,
                                            std::string &error) {
  if (target.requestedSamples == samples)
    return true;
  const auto replacement =
      resources_.createRenderTarget({.width = target.width,
                                     .height = target.height,
                                     .depth = target.depth,
                                     .debugName = id,
                                     .msaaSamples = samples},
                                    error);
  if (!replacement.frameBuffer || !replacement.color)
    return false;
  overlay_.setExternalTexture(id, {.handle = replacement.color,
                                   .width = target.width,
                                   .height = target.height});
  if (target.handles.frameBuffer)
    resources_.destroy(target.handles.frameBuffer);
  if (target.handles.depth)
    resources_.destroy(target.handles.depth);
  if (target.handles.color)
    resources_.destroy(target.handles.color);
  target.handles = replacement;
  target.requestedSamples = samples;
  return true;
}

bool BgfxRenderer3D::beginAssetPreparation(
    const AssetRegistry &registry, JobSystem &jobs,
    std::vector<std::string> &diagnostics, const bool prepareMaterialImages) {
  cancelAssetPreparation();
  preparedTextureVariants_.clear();
  textures_.clear();
  textureSources_.clear();
  materialTextureUse_.clear();
  texturePresentationSerial_ = 0;
  successfulTexturePresentation_ = 0;
  shadows_.clearTargets();
  // A pose cache must not outlive the model data it was sampled from.
  std::erase_if(dynamicMeshes_, [](const auto &entry) {
    return !entry.second->animationModel.empty();
  });
  modelMeshes_.clear();
  deformedMeshes_.clear();
  reliefMeshes_.loadAssets(registry);
  animatedModels_.clear();
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

  bool success = materials_.load(registry, diagnostics);
  terrainGpuFields_.invalidateMaterials();
  terrainMaterials_.loadAssets(registry);
  success = overlay_.loadAssets(registry, diagnostics) && success;
  for (const auto &asset : registry.assets) {
    const auto *material = materials_.find(asset.id);
    if (!material) {
      continue;
    }
    for (const auto &[slot, id] : material->surface.maps) {
      if (material->program && slot != assets::MaterialMapSlot::Albedo) {
        continue;
      }
      if (!material->program &&
          material->surface.mapping == assets::MaterialMapping::Triplanar) {
        const auto *image = findAsset(registry, id);
        if (image && image->type == "Texture2D" &&
            textureSampling2D(*image).wrap == TextureWrap::Clamp) {
          diagnostics.push_back(asset.id + ": triplanar map " + id +
                                " is clamped; set its Texture2D Wrap to Repeat "
                                "for tiled surfaces");
        }
      }
    }
  }
  if (!success) {
    return false;
  }
  auto preparation = std::make_shared<AssetPreparation>();
  preparation->registry = registry;
  preparation->jobs = &jobs;
  std::set<std::pair<std::string, ImageMipSemantic>> requests;
  std::set<std::string> mappedIds;
  for (const auto &asset : registry.assets) {
    const auto *material = materials_.find(asset.id);
    if (!material) {
      continue;
    }
    for (const auto &[slot, id] : material->surface.maps) {
      if (material->program && slot != assets::MaterialMapSlot::Albedo) {
        continue;
      }
      mappedIds.insert(id);
      requests.emplace(id, materialMapSemantic(slot));
    }
  }
  for (const auto &asset : registry.assets) {
    if (asset.type == "Texture2D" && !mappedIds.contains(asset.id)) {
      requests.emplace(asset.id, ImageMipSemantic::Color);
    }
  }
  if (prepareMaterialImages) {
    for (const auto &[id, semantic] : requests) {
      preparation->textures.push_back({id, semantic});
    }
  }
  for (const auto &asset : registry.assets) {
    const bool isModel =
        asset.type == "Model3D" && (asset.sourcePath.extension() == ".gltf" ||
                                    asset.sourcePath.extension() == ".glb");
    preparation->totalUnits += isModel ? 2U : 1U;
  }
  for (const auto &request : preparation->textures) {
    const auto *asset = findAsset(registry, request.id);
    if (asset && asset->type == "Texture2D") {
      preparation->totalUnits += 2U;
    }
  }
  assetPreparation_ = std::move(preparation);
  return true;
}

bool BgfxRenderer3D::stepAssetPreparation(
    const double budgetMilliseconds, bool &complete,
    std::vector<std::string> &diagnostics) {
  complete = false;
  if (!assetPreparation_) {
    complete = true;
    return true;
  }
  auto &preparation = *assetPreparation_;
  const auto started = std::chrono::steady_clock::now();
  const auto exhausted = [&] {
    const auto elapsed = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - started)
                             .count();
    return budgetMilliseconds >= 0.0 && elapsed >= budgetMilliseconds;
  };
  const auto workerReady = [&] {
    if (!preparation.job.ready()) {
      return false;
    }
    preparation.job.wait();
    preparation.job = {};
    return true;
  };
  try {
    while (preparation.nextAsset < preparation.registry.assets.size()) {
      const auto &asset = preparation.registry.assets[preparation.nextAsset];
      const bool isModel =
          asset.type == "Model3D" && (asset.sourcePath.extension() == ".gltf" ||
                                      asset.sourcePath.extension() == ".glb");
      if (isModel) {
        if (!preparation.model) {
          auto model = std::make_shared<PreparedModelAsset>();
          preparation.model = model;
          preparation.job = preparation.jobs->submit(
              [asset, model, stop = preparation.cancellation.get_token()] {
                prepareModelAsset(asset, *model, stop);
              });
          return true;
        }
        if (!workerReady()) {
          return true;
        }
        auto &model = *preparation.model;
        if (!model.error.empty()) {
          diagnostics.push_back(asset.id + ": " + model.error);
          return false;
        }
        for (const auto &diagnostic : model.diagnostics) {
          diagnostics.push_back(asset.id + ": " + diagnostic);
        }
        auto cached = std::make_unique<CachedMesh>(resources_);
        auto &geometry = model.geometry;
        std::string error;
        if (!cached->gpu.upload(geometry.positions, geometry.uvs,
                                geometry.indices, 0xffffffffU, error,
                                model.normals, geometry.colors)) {
          diagnostics.push_back(asset.id + ": " + error);
          return false;
        }
        cached->bounds = meshBounds3D(geometry.positions);
        cached->restGeometry = std::move(geometry);
        if (gpuSkinningEnabled_ && !model.skinVertices.empty()) {
          cached->gpuSkin = std::make_unique<GpuSkinnedMesh3D>(resources_);
          if (!cached->gpuSkin->upload(model.skinVertices,
                                       model.animated->indices,
                                       std::move(model.skinLayout), error)) {
            diagnostics.push_back(asset.id + ": " + error);
            return false;
          }
        }
        modelUnlit_[asset.id] = model.profile->materialPolicy == "unlit";
        modelMeshes_.emplace(asset.id, std::move(cached));
        for (auto &material : model.materials) {
          for (auto &texture : material.textures) {
            if (!textures_.uploadPrepared(texture.id, texture.texture, error)) {
              diagnostics.push_back(asset.id + ": " + error);
              return false;
            }
            const auto materialIndex = material.sourceMaterial;
            TextureSource source;
            source.sampling = texture.texture.sampling;
            source.prepareImported =
                [asset, materialIndex, slot = texture.slot](
                    ImageMipSemantic semantic, PreparedTexture2D &output,
                    std::string &readError, std::stop_token stop) {
                  return reloadImportedTexture(asset, materialIndex, slot,
                                               semantic, output, readError,
                                               stop);
                };
            textureSources_.emplace(texture.id, std::move(source));
            preparedTextureVariants_.emplace(
                texture.id, texture.texture.sampling.mipSemantic);
          }
          materials_.publishImported(material.id, std::move(material.binding));
        }
        if (!model.animated->materialSections.empty()) {
          auto sections = std::make_shared<MeshMaterialSections3D>();
          sections->indices =
              std::make_shared<const std::vector<std::uint32_t>>(
                  model.animated->indices);
          for (const auto &section : model.animated->materialSections) {
            sections->sections.push_back(
                {.id = std::to_string(sections->sections.size()),
                 .material = importedMaterialId(asset.id, section.material),
                 .firstIndex = section.firstIndex,
                 .indexCount = section.indexCount});
          }
          modelMeshes_.at(asset.id)->materialSections = std::move(sections);
        }
        if (model.animated && (!model.animated->clips.empty() ||
                               !model.animated->skins.empty())) {
          animatedModels_.emplace(asset.id, std::move(*model.animated));
        }
        preparation.model.reset();
      } else if (asset.type == "Texture2D") {
        textureSources_.emplace(
            asset.id, TextureSource{.path = asset.sourcePath,
                                    .sampling = textureSampling2D(asset)});
      } else if (asset.type == "RenderTarget") {
        const auto descriptor = assets::loadRenderTargetAsset(asset.sourcePath);
        if (!descriptor) {
          diagnostics.push_back(asset.id + ": could not load render target");
          return false;
        }
        std::string error;
        const auto handles = resources_.createRenderTarget(
            {.width = static_cast<std::uint16_t>(descriptor->width),
             .height = static_cast<std::uint16_t>(descriptor->height),
             .colorFormat = TextureFormat::RGBA8,
             .depth = descriptor->depth,
             .debugName = asset.id},
            error);
        if (!handles.frameBuffer || !handles.color) {
          diagnostics.push_back(asset.id + ": " + error);
          return false;
        }
        renderTargets_.emplace(
            asset.id,
            RenderTarget{.handles = handles,
                         .width = static_cast<std::uint16_t>(descriptor->width),
                         .height =
                             static_cast<std::uint16_t>(descriptor->height),
                         .depth = descriptor->depth});
        overlay_.setExternalTexture(
            asset.id,
            {.handle = handles.color,
             .width = static_cast<std::uint16_t>(descriptor->width),
             .height = static_cast<std::uint16_t>(descriptor->height)});
      }
      preparation.completedUnits += isModel ? 2U : 1U;
      ++preparation.nextAsset;
      if (exhausted()) {
        return true;
      }
    }
    while (preparation.nextTexture < preparation.textures.size()) {
      const auto &request = preparation.textures[preparation.nextTexture];
      const auto source = textureSources_.find(request.id);
      // Live render targets have no encoded image; the target itself is ready.
      if (source == textureSources_.end()) {
        ++preparation.nextTexture;
        continue;
      }
      if (!preparation.image) {
        auto image = std::make_shared<AssetPreparation::TextureResult>();
        preparation.image = image;
        auto descriptor = source->second;
        descriptor.sampling.mipSemantic = request.semantic;
        preparation.job = preparation.jobs->submit(
            [descriptor, image, stop = preparation.cancellation.get_token()] {
              if (stop.stop_requested()) {
                return;
              }
              (void)prepareTextureSource(descriptor,
                                         descriptor.sampling.mipSemantic,
                                         image->texture, image->error, stop);
            });
        return true;
      }
      if (!workerReady()) {
        return true;
      }
      auto &image = *preparation.image;
      if (!image.error.empty() ||
          !textures_.uploadPrepared(request.id, image.texture, image.error)) {
        diagnostics.push_back(request.id + ": " + image.error);
        return false;
      }
      preparedTextureVariants_.emplace(request.id, request.semantic);
      preparation.image.reset();
      preparation.completedUnits += 2U;
      ++preparation.nextTexture;
      if (exhausted()) {
        return true;
      }
    }
  } catch (const std::exception &error) {
    diagnostics.push_back(std::string("Asset preparation failed: ") +
                          error.what());
    return false;
  }
  assetPreparation_.reset();
  complete = true;
  return true;
}

void BgfxRenderer3D::cancelAssetPreparation() {
  cancelTextureVariantPreparation();
  // Jobs own their CPU payload and source descriptors, never this renderer.
  // Dropping a candidate cannot wait on decoding or touch an active renderer.
  if (assetPreparation_) {
    assetPreparation_->cancellation.request_stop();
    assetPreparation_.reset();
  }
}

void BgfxRenderer3D::cancelTextureVariantPreparation() {
  if (textureVariantPreparation_) {
    textureVariantPreparation_->cancellation.request_stop();
    textureVariantPreparation_.reset();
  }
}

bool BgfxRenderer3D::prepareTextureVariant(const std::string_view id,
                                           const ImageMipSemantic semantic,
                                           bool &complete, std::string &error) {
  complete = false;
  error.clear();
  // Finish an earlier camera's request before starting another. Multiple
  // cameras can need different skies without cancelling each other's jobs.
  if (textureVariantPreparation_) {
    const auto pending = textureVariantPreparation_;
    if (!pending->job.ready()) {
      return true;
    }
    auto &result = *pending->result;
    try {
      pending->job.wait();
    } catch (const std::exception &failure) {
      result.error = failure.what();
    }
    const auto variant = static_cast<std::size_t>(pending->semantic);
    if (!result.error.empty() ||
        !textures_.uploadPrepared(pending->id, result.texture, result.error)) {
      if (result.error.empty()) {
        result.error = "Texture variant upload failed.";
      }
      if (const auto source = textureSources_.find(pending->id);
          source != textureSources_.end()) {
        source->second.failedVariants[variant] = true;
        source->second.variantErrors[variant] = result.error;
      }
      error = pending->id + ": " + result.error;
      textureVariantPreparation_.reset();
      return false;
    }
    preparedTextureVariants_.emplace(pending->id, pending->semantic);
    textureVariantPreparation_.reset();
  }
  if (textures_.find(id, semantic).handle) {
    preparedTextureVariants_.emplace(std::string(id), semantic);
    complete = true;
    return true;
  }
  if (renderTargets_.contains(std::string(id))) {
    complete = true;
    return true;
  }
  const auto source = textureSources_.find(std::string(id));
  if (source == textureSources_.end()) {
    error = "Texture source is not loaded: " + std::string(id);
    return false;
  }
  const auto variant = static_cast<std::size_t>(semantic);
  if (source->second.failedVariants[variant]) {
    error = std::string(id) + ": " + source->second.variantErrors[variant];
    return false;
  }
  auto pending = std::make_shared<TextureVariantPreparation>();
  pending->id = id;
  pending->semantic = semantic;
  pending->result = std::make_shared<TextureVariantPreparation::Result>();
  auto descriptor = source->second;
  descriptor.sampling.mipSemantic = semantic;
  pending->job =
      extractionJobs_.submit([descriptor, result = pending->result,
                              stop = pending->cancellation.get_token()] {
        if (stop.stop_requested()) {
          return;
        }
        (void)prepareTextureSource(descriptor, descriptor.sampling.mipSemantic,
                                   result->texture, result->error, stop);
      });
  textureVariantPreparation_ = std::move(pending);
  return true;
}

double BgfxRenderer3D::assetPreparationProgress() const {
  if (!assetPreparation_) {
    return 0.0;
  }
  const auto &preparation = *assetPreparation_;
  const std::size_t completedCpuUnit =
      preparation.job && preparation.job.ready() ? 1U : 0U;
  // Models and images each have a CPU preparation and a GPU upload unit.
  // Reserve the final unit for atomic publication, which belongs to the host.
  return static_cast<double>(preparation.completedUnits + completedCpuUnit) /
         static_cast<double>(preparation.totalUnits + 1U);
}

bool BgfxRenderer3D::loadAssets(const AssetRegistry &registry,
                                std::vector<std::string> &diagnostics) {
  if (!beginAssetPreparation(registry, extractionJobs_, diagnostics, false)) {
    return false;
  }
  bool complete = false;
  while (!complete) {
    if (!stepAssetPreparation(-1.0, complete, diagnostics)) {
      cancelAssetPreparation();
      return false;
    }
    if (!complete) {
      std::this_thread::yield();
    }
  }
  return true;
}

} // namespace demi::runtime::render
