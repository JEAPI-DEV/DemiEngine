#pragma once

#include "demi/core/StringHash.h"

#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/ImageDecoder2D.h"
#include "demi/runtime/render/backend/ImageMipmaps2D.h"

#include <array>
#include <functional>

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
  // Color image variants return linear RGB from hardware sampling. Live
  // presentation targets and raw data/display variants do not.
  bool srgbDecoded = false;
};

struct TextureSampling2D {
  TextureFilter filter = TextureFilter::Linear;
  TextureWrap wrap = TextureWrap::Clamp;
  bool mipmaps = false;
  ImageMipSemantic mipSemantic = ImageMipSemantic::Linear;
};

struct TextureSource2D {
  using Reader = std::function<std::vector<std::byte>(std::string &)>;
  using FailureReporter = std::function<void(std::string_view)>;
  Reader read;
  TextureSampling2D sampling;
  FailureReporter reportFailure;
};

class TextureLibrary2D {
public:
  explicit TextureLibrary2D(GpuResources &resources);
  ~TextureLibrary2D();

  TextureLibrary2D(const TextureLibrary2D &) = delete;
  TextureLibrary2D &operator=(const TextureLibrary2D &) = delete;

  // Registration keeps identity/sampling, without reading or allocating images.
  [[nodiscard]] bool registerSource(std::string id, TextureSource2D source,
                                    std::string &error);
  // Explicit draw-time resolution caches each semantic variant or its failure.
  [[nodiscard]] TextureView2D
  resolve(std::string_view id, std::string &error,
          ImageMipSemantic semantic = ImageMipSemantic::Linear);
  [[nodiscard]] TextureView2D
  resolve(std::string_view id,
          ImageMipSemantic semantic = ImageMipSemantic::Linear);
  [[nodiscard]] bool contains(std::string_view id) const;
  [[nodiscard]] std::size_t availableCount() const;

  [[nodiscard]] bool load(std::string id, std::span<const std::byte> encoded,
                          std::string &error, TextureSampling2D sampling = {});
  [[nodiscard]] bool upload(std::string id, const ImageData2D &image,
                            std::string &error,
                            TextureSampling2D sampling = {});
  [[nodiscard]] TextureView2D
  find(std::string_view id,
       ImageMipSemantic semantic = ImageMipSemantic::Linear) const;
  // Retire only GPU variants. Registered source descriptors remain available.
  using VariantRetention =
      std::function<bool(std::string_view, ImageMipSemantic)>;
  void retainVariants(const VariantRetention &isRetained);

  [[nodiscard]] bool remove(std::string_view id);
  void clear();
  [[nodiscard]] std::size_t size() const { return textures_.size(); }

private:
  struct RegisteredSource {
    TextureSource2D descriptor;
    std::array<std::string, 3> failures;
  };
  GpuResources &resources_;
  std::unordered_map<std::string, RegisteredSource, StringHash, std::equal_to<>>
      sources_;
  std::unordered_map<std::string, std::array<TextureView2D, 3>, StringHash,
                     std::equal_to<>>
      textures_;
};

} // namespace demi::runtime::render
