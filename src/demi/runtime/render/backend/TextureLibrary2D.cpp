#include "demi/runtime/render/backend/TextureLibrary2D.h"
#include "demi/runtime/render/backend/ImageMipmaps2D.h"

#include <utility>

namespace demi::runtime::render {

TextureLibrary2D::TextureLibrary2D(GpuResources &resources)
    : resources_(resources) {}

TextureLibrary2D::~TextureLibrary2D() { clear(); }

bool TextureLibrary2D::load(std::string id,
                            const std::span<const std::byte> encoded,
                            std::string &error,
                            const TextureSampling2D sampling) {
  ImageData2D image;
  return decodeImage2D(encoded, image, error) &&
         upload(std::move(id), image, error, sampling);
}

bool TextureLibrary2D::upload(std::string id, const ImageData2D &image,
                              std::string &error,
                              const TextureSampling2D sampling) {
  if (id.empty() || image.width == 0 || image.height == 0 ||
      image.rgba.size() !=
          static_cast<std::size_t>(image.width) * image.height * 4U) {
    error = "Texture upload requires an ID and complete RGBA image data.";
    return false;
  }
  const ImageData2D &uploadImage = image;
  const auto mipChain = sampling.mipmaps
                            ? imageMipChain2D(image, sampling.mipSemantic)
                            : std::vector<std::byte>{};
  const TextureHandle replacement = resources_.createTexture(
      TextureCreateInfo{.width = uploadImage.width,
                        .height = uploadImage.height,
                        .format = TextureFormat::RGBA8,
                        .data =
                            sampling.mipmaps
                                ? std::span<const std::byte>(mipChain)
                                : std::span<const std::byte>(uploadImage.rgba),
                        .generateMipmaps = sampling.mipmaps,
                        .filter = sampling.filter,
                        .wrap = sampling.wrap,
                        .debugName = id},
      error);
  if (!replacement)
    return false;

  auto &view =
      textures_[std::move(id)][static_cast<std::size_t>(sampling.mipSemantic)];
  if (view.handle) {
    resources_.destroy(view.handle);
  }
  view = {.handle = replacement,
          .width = uploadImage.width,
          .height = uploadImage.height};
  return true;
}

TextureView2D TextureLibrary2D::find(const std::string_view id,
                                     ImageMipSemantic semantic) const {
  const auto found = textures_.find(id);
  return found == textures_.end()
             ? TextureView2D{}
             : found->second[static_cast<std::size_t>(semantic)];
}

bool TextureLibrary2D::remove(const std::string_view id) {
  const auto found = textures_.find(id);
  if (found == textures_.end())
    return false;
  for (const auto &view : found->second) {
    if (view.handle) {
      resources_.destroy(view.handle);
    }
  }
  textures_.erase(found);
  return true;
}

void TextureLibrary2D::clear() {
  for (const auto &[id, variants] : textures_) {
    static_cast<void>(id);
    for (const auto &view : variants) {
      if (view.handle) {
        resources_.destroy(view.handle);
      }
    }
  }
  textures_.clear();
}

} // namespace demi::runtime::render
