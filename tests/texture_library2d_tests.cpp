#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/ImageMipmaps2D.h"
#include "demi/runtime/render/backend/RenderAssetLoading.h"
#include "demi/runtime/render/backend/TextureLibrary2D.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

using namespace demi::runtime::render;

namespace {

void materialMipSemanticsPreserveColorAndNormalMeaning() {
  ImageData2D image{.width = 2,
                    .height = 1,
                    .rgba = {std::byte{0}, std::byte{0}, std::byte{0},
                             std::byte{0}, std::byte{255}, std::byte{255},
                             std::byte{255}, std::byte{255}}};
  const auto linear = imageMipChain2D(image, ImageMipSemantic::Linear);
  const auto color = imageMipChain2D(image, ImageMipSemantic::Color);
  assert(linear.size() == 12 && color.size() == 12);
  for (std::size_t channel = 0; channel < 3; ++channel) {
    assert(linear[8 + channel] == std::byte{128});
    // Half linear light encodes to approximately 0.735 sRGB, not 0.5.
    assert(color[8 + channel] == std::byte{188});
  }
  assert(color[11] == std::byte{128});
  assert(std::equal(image.rgba.begin(), image.rgba.end(), color.begin()));

  image.rgba = {std::byte{255}, std::byte{128}, std::byte{128}, std::byte{255},
                std::byte{128}, std::byte{255}, std::byte{128}, std::byte{255}};
  const auto normal = imageMipChain2D(image, ImageMipSemantic::Normal);
  assert(normal[8] == std::byte{218} && normal[9] == std::byte{218});
  assert(normal[10] == std::byte{128} && normal[11] == std::byte{255});
  image.rgba = {std::byte{255}, std::byte{128}, std::byte{128}, std::byte{255},
                std::byte{0},   std::byte{127}, std::byte{127}, std::byte{255}};
  const auto opposing = imageMipChain2D(image, ImageMipSemantic::Normal);
  assert(opposing[8] == std::byte{128} && opposing[9] == std::byte{128});
  assert(opposing[10] == std::byte{255});
}

} // namespace

int main() {
  materialMipSemanticsPreserveColorAndNormalMeaning();
  assert(imageMipChain2D({}).empty());
  ImageData2D square{
      .width = 2, .height = 2, .rgba = std::vector<std::byte>(16)};
  for (int channel = 0; channel < 4; ++channel)
    square.rgba[channel] = std::byte{255};
  const auto squareChain = imageMipChain2D(square);
  assert(squareChain.size() == 20);
  for (int channel = 0; channel < 4; ++channel)
    assert(squareChain[16 + channel] == std::byte{64});
  ImageData2D odd{.width = 3, .height = 1, .rgba = std::vector<std::byte>(12)};
  for (int channel = 0; channel < 4; ++channel)
    odd.rgba[8 + channel] = std::byte{255};
  const auto oddChain = imageMipChain2D(odd);
  assert(oddChain.size() == 16);
  for (int channel = 0; channel < 4; ++channel)
    assert(oddChain[12 + channel] == std::byte{85});
  const auto fixture =
      std::filesystem::temp_directory_path() / "demi-render-asset-loading.bin";
  {
    std::ofstream output(fixture, std::ios::binary);
    output << "render-data";
  }
  assert(readRenderAssetBytes(fixture).size() == 11);
  assert(readRenderAssetBytes(fixture.string() + ".missing").empty());
  std::filesystem::remove(fixture);

  demi::AssetManifest manifest;
  manifest.textureSettings.filter = "nearest";
  manifest.textureSettings.wrap = "mirror";
  manifest.textureSettings.mipmaps = true;
  const TextureSampling2D sampling =
      textureSampling2D(manifest, TextureFilter::Linear);
  assert(sampling.filter == TextureFilter::Nearest);
  assert(sampling.wrap == TextureWrap::Mirror);
  assert(sampling.mipmaps);

  BgfxGraphicsDevice graphics;
  std::string error;
  assert(graphics.initialize(
      GraphicsDeviceConfig{
          .api = GraphicsApi::Noop, .width = 8, .height = 8, .vsync = false},
      error));
  auto resources = createBgfxGpuResources();
  TextureLibrary2D textures(*resources);

  assert(!textures.upload("", {}, error));
  ImageData2D image{
      .width = 1,
      .height = 1,
      .rgba = {std::byte{0xff}, std::byte{0}, std::byte{0}, std::byte{0xff}}};
  assert(imageMipChain2D(image) == image.rgba);
  assert(textures.upload("asset://red", image, error, sampling));
  const TextureView2D first = textures.find("asset://red");
  assert(first.handle && first.width == 1 && first.height == 1);
  constexpr std::string_view textureId = "asset://red";
  const std::string wrappedTextureId = "prefix:asset://red:suffix";
  const auto textureIdView =
      std::string_view(wrappedTextureId).substr(7, textureId.size());
  assert(textures.find(textureIdView).handle == first.handle);
  assert(!textures
              .find(std::string_view(wrappedTextureId)
                        .substr(7, textureId.size() + 1))
              .handle);
  assert(!textures.find(std::string_view{}).handle);

  ImageData2D invalid = image;
  invalid.rgba.pop_back();
  assert(!textures.upload("asset://red", invalid, error));
  assert(textures.find("asset://red").handle == first.handle);

  image.width = 2;
  image.rgba.resize(8, std::byte{0xff});
  assert(textures.upload("asset://red", image, error));
  const TextureView2D replacement = textures.find("asset://red");
  assert(replacement.handle && replacement.handle != first.handle);
  assert(replacement.width == 2);
  auto colorSampling = sampling;
  colorSampling.mipSemantic = ImageMipSemantic::Color;
  assert(textures.upload("asset://red", image, error, colorSampling));
  const auto colorView = textures.find("asset://red", ImageMipSemantic::Color);
  assert(colorView.handle && colorView.handle != replacement.handle);
  assert(textures.find("asset://red").handle == replacement.handle);
  assert(!textures.find("asset://red", ImageMipSemantic::Normal).handle);
  // A failed replacement must preserve every existing semantic view.
  assert(!textures.upload("asset://red", invalid, error, colorSampling));
  assert(textures.find("asset://red", ImageMipSemantic::Color).handle ==
         colorView.handle);
  assert(textures.size() == 1);
  assert(!textures.remove(
      std::string_view(wrappedTextureId).substr(7, textureId.size() + 1)));
  assert(textures.find(textureIdView).handle == replacement.handle);
  assert(textures.remove(textureIdView));
  assert(!textures.remove("asset://red"));
  assert(!textures.find("asset://red").handle);
  assert(!textures.find("asset://red", ImageMipSemantic::Color).handle);

  textures.clear();
  resources.reset();
  graphics.shutdown();
  return 0;
}
