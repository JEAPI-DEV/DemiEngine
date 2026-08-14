#include "demi/runtime/render/backend/RenderAssetLoading.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace demi::runtime::render {

std::vector<std::byte> readRenderAssetBytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return {};
  const std::vector<char> source((std::istreambuf_iterator<char>(input)), {});
  std::vector<std::byte> result(source.size());
  std::ranges::transform(source, result.begin(), [](const char value) {
    return static_cast<std::byte>(static_cast<unsigned char>(value));
  });
  return result;
}

TextureSampling2D textureSampling2D(const AssetManifest &asset,
                                    const TextureFilter defaultFilter) {
  TextureSampling2D result{.filter = defaultFilter};
  if (asset.textureSettings.filter == "nearest")
    result.filter = TextureFilter::Nearest;
  else if (asset.textureSettings.filter == "bilinear" ||
           asset.textureSettings.filter == "trilinear")
    result.filter = TextureFilter::Linear;

  if (asset.textureSettings.wrap == "repeat")
    result.wrap = TextureWrap::Repeat;
  else if (asset.textureSettings.wrap == "mirror")
    result.wrap = TextureWrap::Mirror;
  return result;
}

} // namespace demi::runtime::render
