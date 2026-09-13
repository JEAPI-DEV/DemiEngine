#include "demi/runtime/scene/ProjectBuildValidation.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/filesystem/ProjectPaths.h"

#include <bimg/decode.h>
#include <bx/allocator.h>
#include <bx/error.h>

#include <algorithm>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <regex>
#include <string>
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

namespace {

void addFeatureUse(std::vector<std::string> &evidence, std::string source) {
  constexpr std::size_t MaximumEvidence = 4;
  if (evidence.size() < MaximumEvidence)
    evidence.push_back(std::move(source));
}

std::string readFileText(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  return std::string{std::istreambuf_iterator<char>(input),
                     std::istreambuf_iterator<char>()};
}

bool skippedFeatureScanRoot(const std::filesystem::path &relative) {
  if (relative.empty())
    return false;
  const std::string first = relative.begin()->string();
  return first == "assets" || first == "build" || first == "generated" ||
         first == ".git" || first == ".demi" || first == ".gradle" ||
         first == ".cxx" || first == "__pycache__" || first == "saves" ||
         first == "tests";
}

bool sceneDocumentFile(const std::filesystem::path &path) {
  return isSceneFile(path) || isPrefabFile(path) || isUiPrefabFile(path) ||
         isHudFile(path);
}

bool luaServiceCall(const std::string &text, const std::string &service) {
  const std::regex pattern("\\b" + service + "\\.\\w+\\s*\\(");
  return std::regex_search(text, pattern);
}

void recordPathEvidence(const std::filesystem::path &path,
                        const std::filesystem::path &projectDirectory,
                        std::vector<std::string> &evidence) {
  addFeatureUse(evidence, path.lexically_relative(projectDirectory).string());
}

// Runtime feature required by a referenced asset. Branding-only asset types
// are excluded: SVG icon/splash sources are rasterized or installed by the
// packager and never decoded by the engine runtime.
std::optional<std::string_view> referencedFeature(const AssetRegistry &registry,
                                                  const std::string &id) {
  const AssetManifest *asset = findAsset(registry, id);
  if (asset == nullptr)
    return std::nullopt;
  if (asset->type == "VideoClip")
    return "media";
  if (asset->type == "SvgTexture2D" || asset->type == "Icon2D")
    return "svg";
  return std::nullopt;
}

void recordAssetReference(const AssetRegistry &registry, const std::string &id,
                          ProjectFeatureUsage &usage) {
  const auto feature = referencedFeature(registry, id);
  if (!feature)
    return;
  if (*feature == "media") {
    usage.media = true;
    addFeatureUse(usage.mediaEvidence, "asset " + id);
  } else if (*feature == "svg") {
    usage.svg = true;
    addFeatureUse(usage.svgEvidence, "asset " + id);
  }
}

void recordAssetReferencesInText(const AssetRegistry &registry,
                                 const std::string &text,
                                 ProjectFeatureUsage &usage) {
  static const std::regex pattern("asset://[A-Za-z0-9_/.-]+");
  for (std::sregex_iterator iterator(text.begin(), text.end(), pattern), end;
       iterator != end; ++iterator)
    recordAssetReference(registry, iterator->str(), usage);
}

} // namespace

ProjectFeatureUsage scanProjectFeatureUsage(
    const nlohmann::json &project,
    const std::filesystem::path &projectDirectory,
    const AssetRegistry &registry) {
  ProjectFeatureUsage usage;

  if (auto contract = project.find("network_contract");
      contract != project.end() && contract->is_string() &&
      !contract->get<std::string>().empty()) {
    usage.network = true;
    addFeatureUse(usage.networkEvidence,
                  "project network_contract " + contract->get<std::string>());
  }

  for (const AssetManifest &asset : registry.assets)
    if (asset.type == "NetworkContract") {
      usage.network = true;
      addFeatureUse(usage.networkEvidence, "asset " + asset.id);
    }

  if (auto resident = project.find("assets");
      resident != project.end() && resident->is_array())
    for (const auto &entry : *resident)
      if (entry.is_string())
        recordAssetReference(registry, entry.get<std::string>(), usage);

  std::error_code scanError;
  for (std::filesystem::recursive_directory_iterator iterator(
           projectDirectory, scanError),
       end;
       !scanError && iterator != end; iterator.increment(scanError)) {
    const std::filesystem::path path = iterator->path();
    const auto relative =
        std::filesystem::relative(path, projectDirectory, scanError);
    if (scanError)
      break;
    if (skippedFeatureScanRoot(relative)) {
      if (iterator->is_directory())
        iterator.disable_recursion_pending();
      continue;
    }
    if (!iterator->is_regular_file())
      continue;

    if (path.extension() == ".lua") {
      const std::string text = readFileText(path);
      if (luaServiceCall(text, "Network") ||
          luaServiceCall(text, "NetworkSession")) {
        usage.network = true;
        recordPathEvidence(path, projectDirectory, usage.networkEvidence);
      }
      if (luaServiceCall(text, "Video")) {
        usage.media = true;
        recordPathEvidence(path, projectDirectory, usage.mediaEvidence);
      }
      recordAssetReferencesInText(registry, text, usage);
      continue;
    }

    if (sceneDocumentFile(path)) {
      const std::string text = readFileText(path);
      if (text.find("\"VideoPlayer\"") != std::string::npos) {
        usage.media = true;
        recordPathEvidence(path, projectDirectory, usage.mediaEvidence);
      }
      recordAssetReferencesInText(registry, text, usage);
    }
  }

  return usage;
}

Diagnostics validateProjectPlatformCapabilities(
    const ProjectBuildSettings &settings, const ProjectFeatureUsage &usage,
    const capabilities::TargetPlatform platform,
    const capabilities::RuntimeFeatures &features,
    const std::filesystem::path &projectPath) {
  Diagnostics diagnostics;
  const std::string platformName{
      capabilities::targetPlatformName(platform)};

  const auto reject = [&](const bool used, const bool supported,
                          const std::vector<std::string> &evidence,
                          const std::string_view code,
                          const std::string_view feature,
                          const std::string_view detail) {
    if (!used || supported)
      return;
    std::string sources;
    for (const std::string &item : evidence) {
      if (!sources.empty())
        sources += ", ";
      sources += item;
    }
    diagnostics.push_back({.severity = Severity::Error,
                           .code = std::string(code),
                           .message = "The " + platformName +
                                      " runtime does not include " +
                                      std::string(feature) +
                                      " support, but the project uses it"
                                      " (" +
                                      sources + ").",
                           .path = projectPath.string(),
                           .suggestion = std::string(detail)});
  };

  reject(usage.network, features.network, usage.networkEvidence,
         "PROJECT_BUILD_FEATURE_NETWORK_UNSUPPORTED", "networking",
         "Enable ENet networking in the packaged runtime or remove network "
         "content from the project.");
  reject(usage.media, features.media, usage.mediaEvidence,
         "PROJECT_BUILD_FEATURE_MEDIA_UNSUPPORTED", "media",
         "Use a target platform with FFmpeg media support or remove video "
         "content from the project.");
  reject(usage.svg, features.svg, usage.svgEvidence,
         "PROJECT_BUILD_FEATURE_SVG_UNSUPPORTED", "runtime SVG",
         "Rasterize the SVG source into a PNG/JPEG/WebP texture or target a "
         "platform with librsvg support.");

  const bool internetDeclared = std::ranges::any_of(
      settings.android.permissions, [](const std::string &permission) {
        return permission == "android.permission.INTERNET";
      });
  // Projects without an authored build block still receive the engine's
  // Gradle default permissions, which include INTERNET.
  if (platform == capabilities::TargetPlatform::Android && settings.authored &&
      usage.network && !internetDeclared) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "PROJECT_BUILD_PERMISSION_NETWORK_MISSING",
                           .message = "The project uses networking but does "
                                      "not declare android.permission.INTERNET.",
                           .path = projectPath.string(),
                           .suggestion =
                               "Add \"android.permission.INTERNET\" to "
                               "build.android.permissions."});
  }

  return diagnostics;
}

Diagnostics validateProjectPlatformCapabilities(
    const std::filesystem::path &projectPath,
    const capabilities::TargetPlatform platform,
    const capabilities::RuntimeFeatures &hostRendererRuntime) {
  std::ifstream input(projectPath);
  if (!input)
    return {};

  nlohmann::json project;
  try {
    input >> project;
  } catch (const nlohmann::json::exception &) {
    return {};
  }

  const auto parsed = parseProjectBuildSettings(project, projectPath);
  if (hasErrors(parsed.diagnostics))
    return {};

  const AssetRegistry registry = loadAssetRegistry(projectPath.parent_path());
  const ProjectFeatureUsage usage =
      scanProjectFeatureUsage(project, projectPath.parent_path(), registry);
  return validateProjectPlatformCapabilities(
      parsed.settings, usage, platform,
      capabilities::targetRuntimeFeatures(platform, hostRendererRuntime),
      projectPath);
}

} // namespace demi::runtime
