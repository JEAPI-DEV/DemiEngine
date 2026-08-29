#include "demi/assets/AssetImporterRegistry.h"

#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

namespace demi::assets {
namespace {

std::string lower(std::string value) {
  std::ranges::transform(value, value.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

bool contains(const std::vector<std::string> &values, std::string_view value) {
  return std::ranges::find(values, value) != values.end();
}

void report(Diagnostics *diagnostics, std::string code, std::string message,
            const std::filesystem::path &path) {
  if (diagnostics != nullptr)
    diagnostics->push_back({.severity = Severity::Error,
                            .code = std::move(code),
                            .message = std::move(message),
                            .path = path.string()});
}

class CopyImporter final : public AssetImporter {
public:
  ImportExecutionResult import(const ImportExecutionRequest &request) override {
    ImportExecutionResult result;
    if (!std::filesystem::is_regular_file(request.source)) {
      result.diagnostics.push_back({.severity = Severity::Error,
                                    .code = "ASSET_IMPORT_SOURCE_NOT_FOUND",
                                    .message = "Importer source is missing.",
                                    .path = request.source.string()});
      return result;
    }
    try {
      if (!nlohmann::json::parse(request.settingsJson).is_object())
        throw std::runtime_error("Importer settings must be a JSON object.");
    } catch (const std::exception &exception) {
      result.diagnostics.push_back({.severity = Severity::Error,
                                    .code = "ASSET_IMPORT_SETTINGS_INVALID",
                                    .message = exception.what(),
                                    .path = request.assetId});
      return result;
    }
    std::error_code error;
    std::filesystem::create_directories(request.outputDirectory, error);
    const auto target = request.outputDirectory / request.source.filename();
    if (!error)
      std::filesystem::copy_file(
          request.source, target,
          std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
      result.diagnostics.push_back({.severity = Severity::Error,
                                    .code = "ASSET_IMPORT_OUTPUT_FAILED",
                                    .message = error.message(),
                                    .path = target.string()});
      return result;
    }
    result.generatedOutputs.push_back(target);
    return result;
  }
};

ImporterDescriptor descriptor(std::string name,
                              std::vector<std::string> extensions,
                              std::vector<std::string> types) {
  return {.name = std::move(name),
          .extensions = std::move(extensions),
          .assetTypes = types,
          .outputTypes = std::move(types),
          .platforms = {"linux", "linux_server", "android"}};
}

} // namespace

bool AssetImporterRegistry::registerImporter(ImporterDescriptor descriptor,
                                             Factory factory,
                                             Diagnostics *diagnostics) {
  if (descriptor.name.empty() || descriptor.version < 1 ||
      descriptor.settingsSchemaVersion < 1 || descriptor.extensions.empty() ||
      descriptor.assetTypes.empty() || !factory) {
    report(diagnostics, "ASSET_IMPORTER_DESCRIPTOR_INVALID",
           "Importer descriptors require identity, positive versions, source "
           "extensions, asset types, and a factory.",
           descriptor.name);
    return false;
  }
  if (std::ranges::any_of(registrations_, [&](const Registration &entry) {
        return entry.descriptor.name == descriptor.name;
      })) {
    report(diagnostics, "ASSET_IMPORTER_DUPLICATE",
           "An importer with this identity is already registered.",
           descriptor.name);
    return false;
  }
  for (std::string &extension : descriptor.extensions) {
    extension = lower(std::move(extension));
    if (!extension.starts_with('.'))
      extension.insert(extension.begin(), '.');
  }
  try {
    if (!nlohmann::json::parse(descriptor.settingsSchema).is_object())
      throw std::runtime_error("Settings schema must be a JSON object.");
  } catch (const std::exception &exception) {
    report(diagnostics, "ASSET_IMPORTER_SETTINGS_SCHEMA_INVALID",
           exception.what(), descriptor.name);
    return false;
  }
  registrations_.push_back(
      {.descriptor = std::move(descriptor), .factory = std::move(factory)});
  std::ranges::sort(registrations_, {}, [](const Registration &entry) {
    return entry.descriptor.name;
  });
  return true;
}

std::optional<ImporterDescriptor> AssetImporterRegistry::select(
    const std::filesystem::path &source, const std::string_view assetType,
    const std::string_view explicitImporter, Diagnostics *diagnostics) const {
  const std::string extension = lower(source.extension().string());
  std::vector<const Registration *> matches;
  for (const Registration &registration : registrations_) {
    const auto &candidate = registration.descriptor;
    if (!explicitImporter.empty() && candidate.name != explicitImporter)
      continue;
    if (!contains(candidate.extensions, extension))
      continue;
    if (!assetType.empty() && !contains(candidate.assetTypes, assetType))
      continue;
    matches.push_back(&registration);
  }
  if (matches.size() == 1) {
    ImporterDescriptor selected = matches.front()->descriptor;
    selected.assetType = assetType.empty() ? selected.assetTypes.front()
                                           : std::string(assetType);
    return selected;
  }
  report(diagnostics,
         matches.empty() ? "ASSET_IMPORTER_NOT_FOUND"
                         : "ASSET_IMPORTER_AMBIGUOUS",
         matches.empty()
             ? "No registered importer supports this source and asset type."
             : "Multiple importers support this source; select one explicitly.",
         source);
  return std::nullopt;
}

std::unique_ptr<AssetImporter>
AssetImporterRegistry::create(const std::string_view name) const {
  const auto found =
      std::ranges::find(registrations_, name, [](const Registration &entry) {
        return entry.descriptor.name;
      });
  return found == registrations_.end() ? nullptr : found->factory();
}

std::vector<ImporterDescriptor> AssetImporterRegistry::descriptors() const {
  std::vector<ImporterDescriptor> result;
  for (const Registration &entry : registrations_)
    result.push_back(entry.descriptor);
  return result;
}

AssetImporterRegistry createBuiltinImporterRegistry() {
  AssetImporterRegistry registry;
  const auto add = [&](ImporterDescriptor value) {
    (void)registry.registerImporter(
        std::move(value), [] { return std::make_unique<CopyImporter>(); });
  };
  add(descriptor("image", {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".qoi"},
                 {"Texture2D", "ImageAnimation2D"}));
  add(descriptor("svg", {".svg"}, {"Icon2D", "SvgTexture2D"}));
  add(descriptor("gif", {".gif"}, {"GifAnimation2D"}));
  add(descriptor("audio", {".wav", ".ogg", ".mp3", ".flac"}, {"AudioClip"}));
  add(descriptor("font", {".ttf", ".otf"}, {"Font2D", "FontAtlas2D"}));
  add(descriptor("gltf-model", {".gltf", ".glb"}, {"Model3D"}));
  add(descriptor("collider-generator", {".gltf", ".glb"}, {"Collider3D"}));
  add(descriptor("model", {".obj", ".iqm", ".m3d"}, {"Model3D"}));
  add(descriptor("video", {".mp4", ".webm", ".mov"}, {"VideoClip"}));
  auto text = descriptor("text", {".txt", ".yaml", ".yml"}, {"Text"});
  text.copyToGeneratedOnImport = false;
  add(std::move(text));
  add(descriptor("json_data", {".json"}, {"DataAsset"}));
  add(descriptor("json_schema", {".json"}, {"DataSchema"}));
  add(descriptor("network_contract", {".json"}, {"NetworkContract"}));
  add(descriptor("material", {".json"}, {"Material"}));
  add(descriptor("shader", {".json"}, {"Shader"}));
  add(descriptor("render_target", {".json"}, {"RenderTarget"}));
  add(descriptor("tilemap2d", {".json"}, {"Tilemap2D"}));
  auto textureAtlas =
      descriptor("texture_atlas", {".json"}, {"TextureAtlas2D"});
  textureAtlas.version = 2;
  add(std::move(textureAtlas));
  add(descriptor("sprite_metadata", {".json"}, {"SpriteMetadata2D"}));
  return registry;
}

const AssetImporterRegistry &builtinImporterRegistry() {
  static const AssetImporterRegistry registry = createBuiltinImporterRegistry();
  return registry;
}

} // namespace demi::assets
