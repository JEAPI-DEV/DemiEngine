#include "demi/runtime/render/BgfxRenderer3D.h"

#include "demi/assets/GltfGeometry.h"
#include "demi/assets/ModelImportProfile.h"
#include "demi/assets/RenderAsset.h"
#include "demi/runtime/render/backend/RenderAssetLoading.h"
#include "demi/runtime/render/bgfx2d/ColorPacking2D.h"

#include <nlohmann/json.hpp>

namespace demi::runtime::render {

bool BgfxRenderer3D::loadAssets(const AssetRegistry &registry,
                                std::vector<std::string> &diagnostics) {
  textures_.clear();
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

  bool success = materials_.load(registry, diagnostics);
  success = overlay_.loadAssets(registry, diagnostics) && success;
  for (const AssetManifest &asset : registry.assets) {
    if (asset.type == "Texture2D") {
      const auto bytes = readRenderAssetBytes(asset.sourcePath);
      std::string error;
      if (bytes.empty() ||
          !textures_.load(asset.id, bytes, error, textureSampling2D(asset))) {
        diagnostics.push_back(
            asset.id + ": " +
            (error.empty() ? "could not read source" : error));
        success = false;
      }
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
                             static_cast<std::uint16_t>(descriptor->height)});
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
        modelMeshes_.emplace(asset.id, std::move(cached));
        std::vector<std::byte> embeddedAlbedo =
            animated ? std::move(animated->albedoImage)
                     : std::vector<std::byte>{};
        if (animated && !animated->clips.empty())
          animatedModels_.emplace(asset.id, std::move(*animated));
        if (profile->materialPolicy != "ignore" &&
            (asset.texturePath || !embeddedAlbedo.empty())) {
          const std::string textureId = asset.id + "#albedo";
          const auto bytes = asset.texturePath
                                 ? readRenderAssetBytes(*asset.texturePath)
                                 : embeddedAlbedo;
          if (bytes.empty() || !textures_.load(textureId, bytes, error,
                                               textureSampling2D(asset))) {
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
