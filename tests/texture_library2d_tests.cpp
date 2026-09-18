#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/ImageMipmaps2D.h"
#include "demi/runtime/render/backend/RenderAssetLoading.h"
#include "demi/runtime/render/backend/TextureLibrary2D.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <string>

using namespace demi::runtime::render;

int main() {
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
  assert(textures.size() == 1);
  assert(textures.remove("asset://red"));
  assert(!textures.remove("asset://red"));
  assert(!textures.find("asset://red").handle);

  textures.clear();
  resources.reset();
  graphics.shutdown();
  return 0;
}
