#include "demi/runtime/assets/HeadlessVisualAssetLoader.h"

#include "demi/runtime/assets/RegistryAssetResourceLoader.h"

namespace demi::runtime {

std::shared_ptr<assets::AssetResourceLoader>
createHeadlessVisualAssetLoader(const AssetRegistry &source) {
  return std::make_shared<RegistryAssetResourceLoader>(
      source, RegistryAssetBackend{
                  .name = "headless-visual",
                  // The visible 3D backend owns this same visual type set; it
                  // includes the 2D backend's types as well as Model3D.
                  .assetTypes = {"Font2D", "GifAnimation2D", "Icon2D",
                                 "ImageAnimation2D", "Material", "Model3D",
                                 "RenderTarget", "Shader", "SvgTexture2D",
                                 "Texture2D", "Tilemap2D"},
                  .apply = [](const AssetRegistry &, std::string &error) {
                    error.clear();
                    return true;
                  }});
}

} // namespace demi::runtime
