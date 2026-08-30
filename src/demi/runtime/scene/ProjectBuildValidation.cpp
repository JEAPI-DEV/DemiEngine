#include "demi/runtime/scene/ProjectBuildValidation.h"

#include "demi/assets/AssetRegistry.h"

#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>

#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace demi::runtime {
namespace {

using ImageDimensions = std::pair<std::uint32_t, std::uint32_t>;

void addError(Diagnostics &diagnostics, std::string code, std::string message,
              const std::filesystem::path &path, std::string suggestion) {
  diagnostics.push_back({.severity = Severity::Error,
                         .code = std::move(code),
                         .message = std::move(message),
                         .path = path.string(),
                         .suggestion = std::move(suggestion)});
}

std::optional<ImageDimensions>
readImageDimensions(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    return std::nullopt;

  const std::vector<std::uint8_t> bytes{
      std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  if (bytes.empty() ||
      bytes.size() > std::numeric_limits<std::uint32_t>::max())
    return std::nullopt;

  bx::DefaultAllocator allocator;
  bx::Error error;
  bimg::ImageContainer *image = bimg::imageParse(
      &allocator, bytes.data(), static_cast<std::uint32_t>(bytes.size()),
      bimg::TextureFormat::Count, &error);
  if (image == nullptr)
    return std::nullopt;

  const ImageDimensions dimensions{image->m_width, image->m_height};
  bimg::imageFree(image);
  return dimensions;
}

void validateBrandingAsset(std::string_view field, const std::string &id,
                           bool square, const AssetRegistry &registry,
                           const std::filesystem::path &projectPath,
                           Diagnostics &diagnostics) {
  if (id.empty())
    return;

  const AssetManifest *asset = findAsset(registry, id);
  if (asset == nullptr) {
    addError(diagnostics, "PROJECT_BUILD_ASSET_NOT_FOUND",
             std::string(field) + " asset was not found: " + id, projectPath,
             "Import the branding asset or correct its stable asset ID.");
    return;
  }

  if (asset->type != "Texture2D" && asset->type != "SvgTexture2D") {
    addError(diagnostics, "PROJECT_BUILD_ASSET_TYPE_INVALID",
             std::string(field) + " must reference a Texture2D or "
                                  "SvgTexture2D asset.",
             asset->manifestPath,
             "Use a supported 2D image asset for release branding.");
    return;
  }

  if (!std::filesystem::is_regular_file(asset->sourcePath)) {
    addError(diagnostics, "PROJECT_BUILD_ASSET_SOURCE_NOT_FOUND",
             std::string(field) + " source image does not exist.",
             asset->sourcePath, "Restore or reimport the source image.");
    return;
  }

  if (asset->type == "SvgTexture2D")
    return;

  const auto dimensions = readImageDimensions(asset->sourcePath);
  if (!dimensions || dimensions->first == 0 || dimensions->second == 0) {
    addError(diagnostics, "PROJECT_BUILD_ASSET_FORMAT_INVALID",
             std::string(field) + " image could not be decoded.",
             asset->sourcePath,
             "Use a supported PNG, JPEG, TGA, DDS, KTX, or PVR image.");
    return;
  }

  if (square && dimensions->first != dimensions->second) {
    addError(diagnostics, "PROJECT_BUILD_ICON_NOT_SQUARE",
             "The application icon must have equal width and height.",
             asset->sourcePath, "Use a square source image for the icon.");
  }
}

} // namespace

Diagnostics validateProjectBuildAssets(
    const ProjectBuildSettings &settings, const AssetRegistry &registry,
    const std::filesystem::path &projectPath) {
  Diagnostics diagnostics;
  if (!settings.authored)
    return diagnostics;

  validateBrandingAsset("icon", settings.icon, true, registry, projectPath,
                        diagnostics);
  validateBrandingAsset("splash", settings.splash, false, registry,
                        projectPath, diagnostics);
  return diagnostics;
}

} // namespace demi::runtime
