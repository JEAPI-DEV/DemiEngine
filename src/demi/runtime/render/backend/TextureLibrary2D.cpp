#include "demi/runtime/render/backend/TextureLibrary2D.h"
#include "demi/runtime/render/backend/ImageMipmaps2D.h"

#include <exception>
#include <utility>

namespace demi::runtime::render {

TextureLibrary2D::TextureLibrary2D(GpuResources &resources)
    : resources_(resources) {}

TextureLibrary2D::~TextureLibrary2D() { clear(); }

bool TextureLibrary2D::registerSource(std::string id, TextureSource2D source,
                                      std::string &error) {
  if (id.empty() || !source.read) {
    error = "Texture source registration requires an ID and a source reader.";
    return false;
  }
  static_cast<void>(remove(id));
  sources_.emplace(std::move(id),
                   RegisteredSource{.descriptor = std::move(source)});
  error.clear();
  return true;
}

TextureView2D TextureLibrary2D::resolve(std::string_view id, std::string &error,
                                        ImageMipSemantic semantic) {
  error.clear();
  if (const auto resident = find(id, semantic); resident.handle) {
    return resident;
  }
  if (id.empty()) {
    return {};
  }
  const auto found = sources_.find(id);
  if (found == sources_.end()) {
    error = "Texture source is unavailable: " + std::string(id);
    return {};
  }
  auto &source = found->second;
  auto &failure = source.failures[static_cast<std::size_t>(semantic)];
  if (!failure.empty()) {
    error = failure;
    return {};
  }
  try {
    const auto encoded = source.descriptor.read(error);
    auto sampling = source.descriptor.sampling;
    sampling.mipSemantic = semantic;
    if (!encoded.empty() && load(std::string(id), encoded, error, sampling)) {
      error.clear();
      return find(id, semantic);
    }
    if (error.empty()) {
      error = "Could not read the texture source.";
    }
  } catch (const std::exception &exception) {
    error = exception.what();
  }
  failure = error;
  if (source.descriptor.reportFailure) {
    source.descriptor.reportFailure(failure);
  }
  return {};
}

TextureView2D TextureLibrary2D::resolve(std::string_view id,
                                        ImageMipSemantic semantic) {
  std::string error;
  return resolve(id, error, semantic);
}

bool TextureLibrary2D::contains(std::string_view id) const {
  return sources_.contains(id) || textures_.contains(id);
}

std::size_t TextureLibrary2D::availableCount() const {
  std::size_t count = textures_.size();
  for (const auto &[id, source] : sources_) {
    if (!textures_.contains(id)) {
      ++count;
    }
  }
  return count;
}

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
                        .srgb = sampling.mipSemantic == ImageMipSemantic::Color,
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
          .height = uploadImage.height,
          .srgbDecoded = sampling.mipSemantic == ImageMipSemantic::Color};
  return true;
}

TextureView2D TextureLibrary2D::find(const std::string_view id,
                                     ImageMipSemantic semantic) const {
  const auto found = textures_.find(id);
  return found == textures_.end()
             ? TextureView2D{}
             : found->second[static_cast<std::size_t>(semantic)];
}

void TextureLibrary2D::retainVariants(const VariantRetention &isRetained) {
  for (auto iterator = textures_.begin(); iterator != textures_.end();) {
    bool hasResidentVariant = false;
    for (std::size_t index = 0; index < iterator->second.size(); ++index) {
      auto &view = iterator->second[index];
      const auto semantic = static_cast<ImageMipSemantic>(index);
      if (view.handle &&
          (!isRetained || !isRetained(iterator->first, semantic))) {
        resources_.destroy(view.handle);
        view = {};
      }
      hasResidentVariant = hasResidentVariant || bool(view.handle);
    }
    if (hasResidentVariant) {
      ++iterator;
    } else {
      iterator = textures_.erase(iterator);
    }
  }
}

bool TextureLibrary2D::remove(const std::string_view id) {
  const auto source = sources_.find(id);
  bool removed = source != sources_.end();
  if (removed) {
    sources_.erase(source);
  }
  const auto found = textures_.find(id);
  if (found != textures_.end()) {
    for (const auto &view : found->second) {
      if (view.handle) {
        resources_.destroy(view.handle);
      }
    }
    textures_.erase(found);
    removed = true;
  }
  return removed;
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
  sources_.clear();
}

} // namespace demi::runtime::render
