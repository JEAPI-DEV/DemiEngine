#include "demi/runtime/render/backend/TextureLibrary2D.h"

#include <utility>

namespace demi::runtime::render {

TextureLibrary2D::TextureLibrary2D(GpuResources &resources)
    : resources_(resources) {}

TextureLibrary2D::~TextureLibrary2D() { clear(); }

bool TextureLibrary2D::load(
    std::string id, const std::span<const std::byte> encoded,
    std::string &error, const TextureSampling2D sampling,
    const std::optional<std::array<std::uint8_t, 3>> colorKey) {
  ImageData2D image;
  return decodeImage2D(encoded, image, error) &&
         upload(std::move(id), image, error, sampling, colorKey);
}

bool TextureLibrary2D::upload(
    std::string id, const ImageData2D &image, std::string &error,
    const TextureSampling2D sampling,
    const std::optional<std::array<std::uint8_t, 3>> colorKey) {
  if (id.empty() || image.width == 0 || image.height == 0 ||
      image.rgba.size() !=
          static_cast<std::size_t>(image.width) * image.height * 4U) {
    error = "Texture upload requires an ID and complete RGBA image data.";
    return false;
  }
  std::optional<ImageData2D> imported;
  if (colorKey) {
    imported = image;
    applyColorKeyTransparency2D(*imported, *colorKey);
  }
  const ImageData2D &uploadImage = imported ? *imported : image;
  const TextureHandle replacement =
      resources_.createTexture(TextureCreateInfo{.width = uploadImage.width,
                                                 .height = uploadImage.height,
                                                 .format = TextureFormat::RGBA8,
                                                 .data = uploadImage.rgba,
                                                 .filter = sampling.filter,
                                                 .wrap = sampling.wrap,
                                                 .debugName = id},
                               error);
  if (!replacement)
    return false;

  const auto existing = textures_.find(id);
  if (existing != textures_.end()) {
    resources_.destroy(existing->second.handle);
    existing->second = {.handle = replacement,
                        .width = uploadImage.width,
                        .height = uploadImage.height};
  } else {
    textures_.emplace(std::move(id),
                      TextureView2D{.handle = replacement,
                                    .width = uploadImage.width,
                                    .height = uploadImage.height});
  }
  return true;
}

TextureView2D TextureLibrary2D::find(const std::string_view id) const {
  const auto found = textures_.find(std::string(id));
  return found == textures_.end() ? TextureView2D{} : found->second;
}

bool TextureLibrary2D::remove(const std::string_view id) {
  const auto found = textures_.find(std::string(id));
  if (found == textures_.end())
    return false;
  resources_.destroy(found->second.handle);
  textures_.erase(found);
  return true;
}

void TextureLibrary2D::clear() {
  for (const auto &[id, texture] : textures_) {
    static_cast<void>(id);
    resources_.destroy(texture.handle);
  }
  textures_.clear();
}

} // namespace demi::runtime::render
