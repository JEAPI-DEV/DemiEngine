#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
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
  const TextureSampling2D sampling =
      textureSampling2D(manifest, TextureFilter::Linear);
  assert(sampling.filter == TextureFilter::Nearest);
  assert(sampling.wrap == TextureWrap::Mirror);

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
  assert(textures.upload("asset://red", image, error));
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
