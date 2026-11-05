#pragma once

#include "demi/assets/GltfMaterials.h"
#include "demi/assets/MaterialSurface.h"
#include "demi/runtime/render/MaterialLibrary.h"
#include "demi/runtime/render/backend/TextureLibrary2D.h"

#include <map>
#include <memory>
#include <stop_token>

namespace demi::runtime::render {

// One model job counts its selected material-slot uses before preparation.
// Unique images decode directly into their working buffers. Shared base pixels
// stay immutable until their last use, which moves them out of this context.
class ImportedImagePreparation3D {
public:
  void includeMaterial(const AssetManifest &asset,
                       const assets::GltfMaterial3D &material);
  [[nodiscard]] bool
  read(const std::shared_ptr<const std::vector<std::byte>> &encoded,
       ImageData2D &output, std::string &error);
  [[nodiscard]] bool read(const std::filesystem::path &path,
                          ImageData2D &output, std::string &error);
  [[nodiscard]] std::size_t decodedImages() const { return decodedImages_; }
  [[nodiscard]] std::size_t retainedBuffers() const { return retainedBuffers_; }
  [[nodiscard]] std::size_t peakRetainedBuffers() const {
    return peakRetainedBuffers_;
  }

private:
  struct ImageUse {
    std::size_t remaining = 0;
    std::optional<ImageData2D> pixels;
  };
  [[nodiscard]] bool readImage(ImageUse &use,
                               std::span<const std::byte> encoded,
                               ImageData2D &output, std::string &error);
  using EncodedImage = std::shared_ptr<const std::vector<std::byte>>;
  std::map<EncodedImage, ImageUse, std::owner_less<EncodedImage>> images_;
  std::map<std::filesystem::path, ImageUse> overrides_;
  std::size_t decodedImages_ = 0;
  std::size_t retainedBuffers_ = 0;
  std::size_t peakRetainedBuffers_ = 0;
};

struct PreparedImportedTexture3D {
  assets::MaterialMapSlot slot;
  std::string id;
  PreparedTexture2D texture;
};

struct PreparedImportedMaterial3D {
  std::string id;
  std::size_t sourceMaterial = 0;
  MaterialBinding binding;
  std::vector<PreparedImportedTexture3D> textures;
};

[[nodiscard]] std::string importedMaterialId(std::string_view model,
                                             std::size_t material);
[[nodiscard]] bool
prepareImportedMaterial(const AssetManifest &asset, std::size_t index,
                        const assets::GltfMaterial3D &material, bool forceUnlit,
                        PreparedImportedMaterial3D &output, std::string &error,
                        std::stop_token stop = {});
[[nodiscard]] bool
prepareImportedMaterial(const AssetManifest &asset, std::size_t index,
                        const assets::GltfMaterial3D &material, bool forceUnlit,
                        ImportedImagePreparation3D &images,
                        PreparedImportedMaterial3D &output, std::string &error,
                        std::stop_token stop = {});
// A source-only identity is retained for demand reloads. No encoded or decoded
// images remain resident solely to recreate an evicted GPU sampling variant.
[[nodiscard]] bool
reloadImportedTexture(const AssetManifest &asset, std::size_t material,
                      assets::MaterialMapSlot slot, ImageMipSemantic semantic,
                      PreparedTexture2D &output, std::string &error,
                      std::stop_token stop = {});

} // namespace demi::runtime::render
