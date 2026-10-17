#include "demi/runtime/render/BgfxRenderer3D.h"

#include "demi/assets/GltfGeometry.h"
#include "demi/assets/ModelImportProfile.h"
#include "demi/assets/RenderAsset.h"
#include "demi/runtime/diagnostics/DeviceLog.h"
#include "demi/runtime/render/backend/RenderAssetLoading.h"
#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"

#include <nlohmann/json.hpp>

namespace demi::runtime::render {

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
  const auto bytes = descriptor.readEmbedded
                         ? descriptor.readEmbedded(error)
                         : readRenderAssetBytes(descriptor.path);
  auto sampling = descriptor.sampling;
  sampling.mipSemantic = semantic;
  if (bytes.empty() ||
      !textures_.load(std::string(id), bytes, error, sampling)) {
    descriptor.failedVariants[variant] = true;
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

bool BgfxRenderer3D::loadAssets(const AssetRegistry &registry,
                                std::vector<std::string> &diagnostics) {
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
  for (const AssetManifest &asset : registry.assets) {
    if (asset.type == "Texture2D") {
      // Source registration does not allocate a 3D copy. The first actual mesh,
      // sky or particle use resolves its own sampling semantic lazily.
      textureSources_.emplace(
          asset.id, TextureSource{.path = asset.sourcePath,
                                  .sampling = textureSampling2D(asset)});
    } else if (asset.type == "RenderTarget") {
      const auto descriptor = assets::loadRenderTargetAsset(asset.sourcePath);
      if (!descriptor) {
        diagnostics.push_back(asset.id + ": could not load render target");
        success = false;
        continue;
      }
      std::string error;
      const RenderTargetHandles handles = resources_.createRenderTarget(
          {.width = static_cast<std::uint16_t>(descriptor->width),
           .height = static_cast<std::uint16_t>(descriptor->height),
           .colorFormat = TextureFormat::RGBA8,
           .depth = descriptor->depth,
           .debugName = asset.id},
          error);
      if (!handles.frameBuffer || !handles.color) {
        diagnostics.push_back(asset.id + ": " + error);
        success = false;
      } else {
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
    } else if (asset.type == "Model3D" &&
               (asset.sourcePath.extension() == ".gltf" ||
                asset.sourcePath.extension() == ".glb")) {
      std::string error;
      std::vector<Vec3> positions;
      std::vector<Vec2> textureCoordinates;
      std::vector<std::uint32_t> vertexColors;
      std::vector<std::uint32_t> indices;
      const nlohmann::json settings =
          nlohmann::json::parse(asset.settingsJson, nullptr, false);
      // Legacy manifests had no import profile and always exposed animation
      // clips. Preserve that behavior while new imports remain explicit.
      const bool hasProfile =
          settings.is_object() && settings.contains("model_import");
      const auto profile =
          hasProfile ? assets::parseModelImportProfile(settings)
                     : std::make_optional(
                           assets::modelImportPreset("animated_character"));
      if (!profile) {
        diagnostics.push_back(asset.id + ": invalid model import profile");
        success = false;
        continue;
      }
      modelUnlit_[asset.id] = profile->materialPolicy == "unlit";
      auto animated =
          assets::loadGltfSkinnedModel3D(asset.sourcePath, *profile, error);
      if (animated) {
        if (!animated->bindPosePositions(positions, error)) {
          diagnostics.push_back(asset.id + ": " + error);
          success = false;
          continue;
        }
        textureCoordinates.reserve(animated->vertices.size());
        vertexColors.reserve(animated->vertices.size());
        for (const assets::GltfSkinnedVertex3D &vertex : animated->vertices) {
          textureCoordinates.push_back(vertex.uv);
          vertexColors.push_back(packVertexColorRgba8(vertex.color));
        }
        indices = animated->indices;
      } else {
        error.clear();
        const auto geometry =
            assets::loadGltfGeometry(asset.sourcePath, *profile, error);
        if (!geometry) {
          diagnostics.push_back(asset.id + ": " + error);
          success = false;
          continue;
        }
        positions.reserve(geometry->triangles.size() * 3U);
        textureCoordinates.reserve(geometry->triangles.size() * 3U);
        for (const assets::GltfTriangle &triangle : geometry->triangles) {
          for (const assets::GltfPoint3 point :
               {triangle.a, triangle.b, triangle.c})
            positions.push_back({point.x, point.y, point.z});
          for (const assets::GltfPoint2 point :
               {triangle.uvA, triangle.uvB, triangle.uvC})
            textureCoordinates.push_back({point.x, point.y});
        }
      }
      auto cached = std::make_unique<CachedMesh>(resources_);
      if (!cached->gpu.upload(positions, textureCoordinates, indices,
                              0xffffffffU, error, {}, vertexColors)) {
        diagnostics.push_back(asset.id + ": " + error);
        success = false;
      } else {
        cached->bounds = meshBounds3D(positions);
        cached->restGeometry = {.positions = std::move(positions),
                                .uvs = std::move(textureCoordinates),
                                .indices = std::move(indices),
                                .colors = std::move(vertexColors)};
        if (gpuSkinningEnabled_ && animated) {
          std::vector<GpuSkinnedVertex3D> skinVertices;
          GpuSkinPaletteLayout layout;
          std::string reason;
          if (buildGpuSkinVertices(*animated, skinVertices, layout, reason)) {
            cached->gpuSkin = std::make_unique<GpuSkinnedMesh3D>(resources_);
            if (!cached->gpuSkin->upload(skinVertices, animated->indices,
                                         std::move(layout), error)) {
              diagnostics.push_back(asset.id + ": " + error);
              success = false;
              cached->gpuSkin.reset();
            }
          }
        }
        modelMeshes_.emplace(asset.id, std::move(cached));
        std::vector<std::byte> embeddedAlbedo =
            animated ? std::move(animated->albedoImage)
                     : std::vector<std::byte>{};
        if (animated && (!animated->clips.empty() || !animated->skins.empty()))
          animatedModels_.emplace(asset.id, std::move(*animated));
        if (profile->materialPolicy != "ignore" &&
            (asset.texturePath || !embeddedAlbedo.empty())) {
          const std::string textureId = asset.id + "#albedo";
          const auto bytes = asset.texturePath
                                 ? readRenderAssetBytes(*asset.texturePath)
                                 : embeddedAlbedo;
          auto sampling = textureSampling2D(asset);
          sampling.mipSemantic = ImageMipSemantic::Color;
          TextureSource imageSource;
          imageSource.sampling = sampling;
          if (asset.texturePath) {
            imageSource.path = *asset.texturePath;
          } else {
            imageSource.readEmbedded = [path = asset.sourcePath,
                                        importProfile =
                                            *profile](std::string &readError) {
              auto model = assets::loadGltfSkinnedModel3D(path, importProfile,
                                                          readError);
              return model ? std::move(model->albedoImage)
                           : std::vector<std::byte>{};
            };
          }
          textureSources_.emplace(textureId, std::move(imageSource));
          if (bytes.empty() ||
              !textures_.load(textureId, bytes, error, sampling)) {
            diagnostics.push_back(asset.id + ": " + error);
            success = false;
          } else {
            modelTextures_[asset.id] = textureId;
          }
        }
      }
    }
  }
  return success;
}

} // namespace demi::runtime::render
