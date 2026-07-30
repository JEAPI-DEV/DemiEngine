#pragma once

#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/ImageDecoder2D.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace demi::runtime::render {

struct TextureView2D {
  TextureHandle handle;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
};

struct TextureSampling2D {
  TextureFilter filter = TextureFilter::Linear;
  TextureWrap wrap = TextureWrap::Clamp;
};

class TextureLibrary2D {
public:
  explicit TextureLibrary2D(GpuResources &resources);
  ~TextureLibrary2D();

  TextureLibrary2D(const TextureLibrary2D &) = delete;
  TextureLibrary2D &operator=(const TextureLibrary2D &) = delete;

  [[nodiscard]] bool load(std::string id, std::span<const std::byte> encoded,
                          std::string &error, TextureSampling2D sampling = {});
  [[nodiscard]] bool upload(std::string id, const ImageData2D &image,
                            std::string &error,
                            TextureSampling2D sampling = {});
  [[nodiscard]] TextureView2D find(std::string_view id) const;
  [[nodiscard]] bool remove(std::string_view id);
  void clear();
  [[nodiscard]] std::size_t size() const { return textures_.size(); }

private:
  GpuResources &resources_;
  std::unordered_map<std::string, TextureView2D> textures_;
};

} // namespace demi::runtime::render
