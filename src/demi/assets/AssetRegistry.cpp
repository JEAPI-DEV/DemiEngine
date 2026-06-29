#include "demi/assets/AssetRegistry.h"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace demi {

namespace {

std::string readFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::optional<std::string> stringAfterKey(const std::string& text, const std::string& key) {
  const std::string quotedKey = "\"" + key + "\"";
  const std::size_t keyPos = text.find(quotedKey);
  if (keyPos == std::string::npos) {
    return std::nullopt;
  }

  const std::size_t colon = text.find(':', keyPos + quotedKey.size());
  if (colon == std::string::npos) {
    return std::nullopt;
  }

  const std::size_t firstQuote = text.find('"', colon + 1);
  if (firstQuote == std::string::npos) {
    return std::nullopt;
  }

  const std::size_t secondQuote = text.find('"', firstQuote + 1);
  if (secondQuote == std::string::npos) {
    return std::nullopt;
  }

  return text.substr(firstQuote + 1, secondQuote - firstQuote - 1);
}

std::vector<std::string> extractReferencesWithPrefix(const std::string& text, const std::string& prefix) {
  std::vector<std::string> references;
  std::size_t cursor = 0;
  while (true) {
    const std::size_t found = text.find(prefix, cursor);
    if (found == std::string::npos) {
      break;
    }

    std::size_t end = found;
    while (end < text.size()) {
      const char c = text[end];
      if (c == '"' || c == '\'' || c == ',' || c == ']' || c == '}' || c == '\n' || c == '\r' || c == ' ' || c == '\t') {
        break;
      }
      ++end;
    }

    const std::string reference = text.substr(found, end - found);
    if (std::ranges::find(references, reference) == references.end()) {
      references.push_back(reference);
    }
    cursor = end;
  }
  return references;
}

} // namespace

std::optional<AssetManifest> loadAssetManifest(const std::filesystem::path& manifestPath, Diagnostic* diagnostic) {
  if (!std::filesystem::exists(manifestPath)) {
    if (diagnostic != nullptr) {
      *diagnostic = Diagnostic{.severity = Severity::Error, .code = "ASSET_MANIFEST_NOT_FOUND", .message = "Asset manifest does not exist.", .path = manifestPath.string()};
    }
    return std::nullopt;
  }

  const std::string text = readFile(manifestPath);
  const std::optional<std::string> id = stringAfterKey(text, "id");
  const std::optional<std::string> type = stringAfterKey(text, "type");
  const std::optional<std::string> source = stringAfterKey(text, "source");
  if (!id.has_value() || !type.has_value() || !source.has_value()) {
    if (diagnostic != nullptr) {
      *diagnostic = Diagnostic{.severity = Severity::Error, .code = "ASSET_MANIFEST_INVALID", .message = "Asset manifest must include id, type, and source.", .path = manifestPath.string()};
    }
    return std::nullopt;
  }

  return AssetManifest{.id = *id, .type = *type, .manifestPath = manifestPath, .sourcePath = manifestPath.parent_path() / *source};
}

AssetRegistry loadAssetRegistry(const std::filesystem::path& projectDirectory) {
  AssetRegistry registry{.projectDirectory = projectDirectory};
  const std::filesystem::path assetsDirectory = projectDirectory / "assets";
  if (!std::filesystem::exists(assetsDirectory)) {
    return registry;
  }

  for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(assetsDirectory)) {
    if (!entry.is_regular_file() || !entry.path().filename().string().ends_with(".asset.json")) {
      continue;
    }

    Diagnostic diagnostic;
    std::optional<AssetManifest> manifest = loadAssetManifest(entry.path(), &diagnostic);
    if (manifest.has_value()) {
      registry.assets.push_back(*manifest);
    } else {
      registry.diagnostics.push_back(diagnostic);
    }
  }

  std::ranges::sort(registry.assets, {}, &AssetManifest::id);
  return registry;
}

const AssetManifest* findAsset(const AssetRegistry& registry, const std::string& id) {
  for (const AssetManifest& asset : registry.assets) {
    if (asset.id == id) {
      return &asset;
    }
  }
  return nullptr;
}

std::vector<std::string> extractAssetReferences(const std::string& text) {
  return extractReferencesWithPrefix(text, "asset://");
}

std::vector<std::string> extractScriptReferences(const std::string& text) {
  return extractReferencesWithPrefix(text, "script://");
}

std::filesystem::path resolveScriptReference(const std::filesystem::path& projectDirectory, const std::string& reference) {
  constexpr std::string_view prefix = "script://";
  if (reference.starts_with(prefix)) {
    return projectDirectory / reference.substr(prefix.size());
  }
  return projectDirectory / reference;
}

} // namespace demi
