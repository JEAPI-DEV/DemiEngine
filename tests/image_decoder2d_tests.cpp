#include "demi/runtime/render/backend/ImageDecoder2D.h"

#include <array>
#include <cassert>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>

using namespace demi::runtime::render;

int main() {
  // 16x16 RGBA PNG from the checked-in voxel test assets.
  constexpr std::array<unsigned char, 90> Png = {
      0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00,
      0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x10,
      0x00, 0x00, 0x00, 0x10, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f,
      0xf3, 0xff, 0x61, 0x00, 0x00, 0x00, 0x21, 0x49, 0x44, 0x41,
      0x54, 0x78, 0xda, 0x63, 0x48, 0x70, 0x35, 0xf8, 0x4f, 0x09,
      0x66, 0x00, 0x11, 0x2d, 0x71, 0x4e, 0x64, 0xe1, 0x51, 0x03,
      0x46, 0x0d, 0x18, 0x35, 0x80, 0xda, 0x06, 0x50, 0x82, 0x01,
      0x06, 0xd5, 0x10, 0x9b, 0x19, 0xdd, 0x52, 0xf4, 0x00, 0x00,
      0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};

  ImageData2D image;
  std::string error;
  assert(!decodeImage2D({}, image, error));
  assert(!error.empty());
  const auto bytes = std::as_bytes(std::span(Png));
  assert(!decodeImage2D(bytes.first(20), image, error));
  assert(image.rgba.empty());
  assert(decodeImage2D(bytes, image, error));
  assert(image.width == 16);
  assert(image.height == 16);
  assert(image.rgba.size() == 16 * 16 * 4);

  const auto decodeText = [&](const std::string_view value) {
    return decodeImage2D(
        std::as_bytes(std::span(value.data(), value.size())), image, error);
  };
  assert(decodeText("P3\n# comment\n2 1\n15\n15 0 0 0 15 7\n"));
  assert(image.width == 2 && image.height == 1);
  assert(image.rgba[0] == std::byte{0xff});
  assert(image.rgba[1] == std::byte{0});
  assert(image.rgba[4] == std::byte{0});
  assert(image.rgba[5] == std::byte{0xff});
  assert(image.rgba[6] == std::byte{119});
  assert(!decodeText("P3\n2 1\n255\n0 0 0\n"));
  assert(image.rgba.empty());
  assert(!decodeText("P3\n0 1\n255\n"));

  constexpr std::array<unsigned char, 17> PpmBinary = {
      'P', '6', '\n', '2', ' ', '1', '\n', '2', '5', '5', '\n',
      255, 128, 0, 0, 64, 255};
  assert(decodeImage2D(std::as_bytes(std::span(PpmBinary)), image, error));
  assert(image.width == 2 && image.height == 1);
  assert(image.rgba[0] == std::byte{0xff});
  assert(image.rgba[1] == std::byte{0x80});
  assert(image.rgba[7] == std::byte{0xff});

  constexpr std::array<unsigned char, 43> Gif = {
      0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01,
      0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff,
      0xff, 0x21, 0xf9, 0x04, 0x01, 0x0a, 0x00, 0x01, 0x00,
      0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
      0x00, 0x02, 0x02, 0x4c, 0x01, 0x00, 0x3b};
  AnimatedImageData2D animation;
  assert(decodeGif2D(std::as_bytes(std::span(Gif)), animation, error));
  assert(animation.frames.size() == 1);
  assert(animation.frameDurations.size() == 1);
  assert(animation.frames.front().width == 1);
  assert(!decodeGif2D(std::as_bytes(std::span(Gif).first(10)), animation,
                      error));
  assert(animation.frames.empty());
  return 0;
}
