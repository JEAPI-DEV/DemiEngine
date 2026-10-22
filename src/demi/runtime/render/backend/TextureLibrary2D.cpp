#include "demi/runtime/render/backend/TextureLibrary2D.h"
#include "demi/runtime/render/backend/ImageMipmaps2D.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace demi::runtime::render {

namespace {

bool validSemantic(const ImageMipSemantic semantic) {
  return semantic == ImageMipSemantic::Linear ||
         semantic == ImageMipSemantic::Color ||
         semantic == ImageMipSemantic::Normal;
}

std::size_t preparedByteCount(const std::uint16_t width,
                              const std::uint16_t height, const bool mipmaps) {
  if (width == 0 || height == 0) {
    return 0;
  }
  unsigned levelWidth = width;
  unsigned levelHeight = height;
  std::size_t bytes = std::size_t(levelWidth) * levelHeight * 4;
  while (mipmaps && (levelWidth > 1 || levelHeight > 1)) {
    levelWidth = std::max(1U, levelWidth / 2);
    levelHeight = std::max(1U, levelHeight / 2);
    bytes += std::size_t(levelWidth) * levelHeight * 4;
  }
  return bytes;
}

} // namespace

bool prepareTexture2D(const std::span<const std::byte> encoded,
                      const TextureSampling2D sampling,
                      PreparedTexture2D &output, std::string &error,
                      const std::stop_token stop) {
  if (stop.stop_requested()) {
    error = "Texture preparation was cancelled.";
    return false;
  }
  ImageData2D image;
  if (!decodeImage2D(encoded, image, error)) {
    return false;
  }
  return prepareTexture2D(image, sampling, output, error, stop);
}

bool prepareTexture2D(const ImageData2D &image,
                      const TextureSampling2D sampling,
                      PreparedTexture2D &output, std::string &error,
                      const std::stop_token stop) {
  if (stop.stop_requested()) {
    error = "Texture preparation was cancelled.";
    return false;
  }
  const auto expectedBytes =
      preparedByteCount(image.width, image.height, false);
  if (expectedBytes == 0 || image.rgba.size() != expectedBytes ||
      !validSemantic(sampling.mipSemantic)) {
    error = "Texture preparation requires complete RGBA image data and a valid "
            "semantic.";
    return false;
  }
  PreparedTexture2D candidate{
      .width = image.width,
      .height = image.height,
      .sampling = sampling,
      .rgbaMipBytes = sampling.mipmaps
                          ? imageMipChain2D(image, sampling.mipSemantic, stop)
                          : image.rgba};
  if (stop.stop_requested()) {
    error = "Texture preparation was cancelled.";
    return false;
  }
  if (candidate.rgbaMipBytes.size() !=
      preparedByteCount(image.width, image.height, sampling.mipmaps)) {
    error = "Texture preparation produced an incomplete mip chain.";
    return false;
  }
  output = std::move(candidate);
  error.clear();
  return true;
}

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
  PreparedTexture2D prepared;
  return prepareTexture2D(encoded, sampling, prepared, error) &&
         uploadPrepared(std::move(id), prepared, error);
}

bool TextureLibrary2D::upload(std::string id, const ImageData2D &image,
                              std::string &error,
                              const TextureSampling2D sampling) {
  PreparedTexture2D prepared;
  return prepareTexture2D(image, sampling, prepared, error) &&
         uploadPrepared(std::move(id), prepared, error);
}

bool TextureLibrary2D::uploadPrepared(std::string id,
                                      const PreparedTexture2D &prepared,
                                      std::string &error) {
  const auto &sampling = prepared.sampling;
  const auto expectedBytes =
      preparedByteCount(prepared.width, prepared.height, sampling.mipmaps);
  if (id.empty() || expectedBytes == 0 ||
      prepared.rgbaMipBytes.size() != expectedBytes ||
      !validSemantic(sampling.mipSemantic)) {
    error =
        "Texture upload requires an ID and complete prepared RGBA mip data.";
    return false;
  }
  const TextureHandle replacement = resources_.createTexture(
      TextureCreateInfo{.width = prepared.width,
                        .height = prepared.height,
                        .format = TextureFormat::RGBA8,
                        .data = prepared.rgbaMipBytes,
                        .generateMipmaps = sampling.mipmaps,
                        .srgb = sampling.mipSemantic == ImageMipSemantic::Color,
                        .filter = sampling.filter,
                        .wrap = sampling.wrap,
                        .debugName = id,
                        .wrapV = sampling.wrapV,
                        .minFilter = sampling.minFilter,
                        .magFilter = sampling.magFilter,
                        .mipFilter = sampling.mipFilter},
      error);
  if (!replacement) {
    return false;
  }
  auto &view =
      textures_[std::move(id)][static_cast<std::size_t>(sampling.mipSemantic)];
  if (view.handle) {
    resources_.destroy(view.handle);
  }
  view = {.handle = replacement,
          .width = prepared.width,
          .height = prepared.height,
          .srgbDecoded = sampling.mipSemantic == ImageMipSemantic::Color};
  error.clear();
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
