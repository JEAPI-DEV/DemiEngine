#pragma once

#include "demi/assets/GltfMaterials.h"
#include "demi/assets/MaterialSurface.h"
#include "demi/runtime/render/MaterialLibrary.h"
#include "demi/runtime/render/backend/TextureLibrary2D.h"

#include <stop_token>

namespace demi::runtime::render {

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
// A source-only identity is retained for demand reloads. No encoded or decoded
// images remain resident solely to recreate an evicted GPU sampling variant.
[[nodiscard]] bool
reloadImportedTexture(const AssetManifest &asset, std::size_t material,
                      assets::MaterialMapSlot slot, ImageMipSemantic semantic,
                      PreparedTexture2D &output, std::string &error,
                      std::stop_token stop = {});

} // namespace demi::runtime::render
