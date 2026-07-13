#include "demi/assets/AssetPackage.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/AssetSourceFiles.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <map>
#include <set>

namespace demi::assets {
namespace {

constexpr std::string_view Magic = "DEMI-ASSET-PACKAGE\n";
constexpr std::uint64_t MaximumHeaderSize = 64ULL * 1024ULL * 1024ULL;

struct PackageFile {
  std::string path;
  std::uint64_t size = 0;
  std::string hash;
  std::vector<unsigned char> bytes;
};

void writeU64(std::ostream &output, std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8)
    output.put(static_cast<char>((value >> shift) & 0xff));
}

bool readU64(std::istream &input, std::uint64_t &value) {
  value = 0;
  for (int shift = 0; shift < 64; shift += 8) {
    const int byte = input.get();
    if (byte == std::char_traits<char>::eof())
      return false;
    value |= static_cast<std::uint64_t>(static_cast<unsigned char>(byte))
             << shift;
  }
  return true;
}

std::optional<std::vector<unsigned char>>
readBytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input)
    return std::nullopt;
  const auto size = input.tellg();
  if (size < 0)
    return std::nullopt;
  std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
  input.seekg(0);
  input.read(reinterpret_cast<char *>(bytes.data()), size);
  return input ? std::make_optional(std::move(bytes)) : std::nullopt;
}

bool safeRelativePath(const std::string &value) {
  const std::filesystem::path path(value);
  if (value.empty() || path.is_absolute())
    return false;
  for (const auto &part : path)
    if (part == ".." || part == ".")
      return false;
  return true;
}

std::optional<std::pair<nlohmann::json, std::vector<PackageFile>>>
readPackage(const std::filesystem::path &path, Diagnostics &diagnostics) {
  std::ifstream input(path, std::ios::binary);
  std::string magic(Magic.size(), '\0');
  input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  std::uint64_t headerSize = 0;
  if (!input || magic != Magic || !readU64(input, headerSize) ||
      headerSize > MaximumHeaderSize) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "ASSET_PACKAGE_INVALID",
                           .message = "Invalid asset package header.",
                           .path = path.string()});
    return std::nullopt;
  }
  std::string headerText(static_cast<std::size_t>(headerSize), '\0');
  input.read(headerText.data(), static_cast<std::streamsize>(headerSize));
  nlohmann::json header;
  try {
    header = nlohmann::json::parse(headerText);
  } catch (const nlohmann::json::exception &error) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "ASSET_PACKAGE_INVALID",
                           .message = error.what(),
                           .path = path.string()});
    return std::nullopt;
  }
  try {
    if (header.value("format_version", 0) != 1 ||
        header.value("package_type", "") != "DemiAssetPackage" ||
        !header.contains("assets") || !header["assets"].is_array() ||
        !header.contains("files") || !header["files"].is_array()) {
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "ASSET_PACKAGE_UNSUPPORTED",
                             .message = "Unsupported asset package format.",
                             .path = path.string()});
      return std::nullopt;
    }
    for (const auto &asset : header["assets"]) {
      if (!asset.is_object() || !asset.contains("id") ||
          !asset["id"].is_string() ||
          !asset["id"].get<std::string>().starts_with("asset://") ||
          !asset.contains("manifest") || !asset["manifest"].is_string() ||
          !safeRelativePath(asset["manifest"].get<std::string>())) {
        diagnostics.push_back({.severity = Severity::Error,
                               .code = "ASSET_PACKAGE_INVALID",
                               .message = "Package asset index is invalid.",
                               .path = path.string()});
        return std::nullopt;
      }
    }
    std::vector<PackageFile> files;
    for (const auto &entry : header["files"]) {
      PackageFile file{.path = entry.value("path", ""),
                       .size = entry.value("size", 0ULL),
                       .hash = entry.value("hash", "")};
      if (!safeRelativePath(file.path) || file.hash.empty()) {
        diagnostics.push_back({.severity = Severity::Error,
                               .code = "ASSET_PACKAGE_UNSAFE_PATH",
                               .message = "Package contains an unsafe path.",
                               .path = file.path});
        return std::nullopt;
      }
      file.bytes.resize(static_cast<std::size_t>(file.size));
      input.read(reinterpret_cast<char *>(file.bytes.data()),
                 static_cast<std::streamsize>(file.size));
      if (!input || hashBytes(file.bytes) != file.hash) {
        diagnostics.push_back({.severity = Severity::Error,
                               .code = "ASSET_PACKAGE_CORRUPT",
                               .message = "Package file checksum failed.",
                               .path = file.path});
        return std::nullopt;
      }
      files.push_back(std::move(file));
    }
    for (const auto &asset : header["assets"]) {
      const auto manifest = asset["manifest"].get<std::string>();
      const bool included =
          std::ranges::any_of(files, [&](const PackageFile &file) {
            return file.path == manifest;
          });
      if (!included) {
        diagnostics.push_back({.severity = Severity::Error,
                               .code = "ASSET_PACKAGE_INVALID",
                               .message = "Package omits an indexed manifest.",
                               .path = manifest});
        return std::nullopt;
      }
    }
    return std::make_pair(std::move(header), std::move(files));
  } catch (const nlohmann::json::exception &error) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "ASSET_PACKAGE_INVALID",
                           .message = error.what(),
                           .path = path.string()});
    return std::nullopt;
  }
}

} // namespace

Diagnostics exportAssetPackage(const AssetPackageExportRequest &request) {
  Diagnostics diagnostics;
  const AssetRegistry registry = loadAssetRegistry(request.projectDirectory);
  const Diagnostics registryDiagnostics = validateAssetRegistry(registry);
  if (hasErrors(registryDiagnostics))
    return registryDiagnostics;
  std::set<std::string> selected;
  for (const auto &id : request.assetIds) {
    const AssetManifest *asset = findAsset(registry, id);
    if (asset == nullptr) {
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "ASSET_PACKAGE_ID_NOT_FOUND",
                             .message = "Asset was not found: " + id,
                             .path = request.projectDirectory.string()});
      continue;
    }
    selected.insert(id);
    for (const auto *dependency :
         assetDependencies(registry, *asset, &diagnostics))
      selected.insert(dependency->id);
  }
  if (hasErrors(diagnostics))
    return diagnostics;

  std::map<std::string, PackageFile> files;
  nlohmann::json assets = nlohmann::json::array();
  for (const auto &id : selected) {
    const AssetManifest *asset = findAsset(registry, id);
    const auto relativeManifest = std::filesystem::relative(
        asset->manifestPath, request.projectDirectory);
    assets.push_back(
        {{"id", id}, {"manifest", relativeManifest.generic_string()}});
    for (const auto &path : collectAssetFiles(*asset)) {
      if (!pathIsInside(request.projectDirectory, path)) {
        diagnostics.push_back({.severity = Severity::Error,
                               .code = "ASSET_PACKAGE_EXTERNAL_FILE",
                               .message = "Asset file is outside the project.",
                               .path = path.string(),
                               .suggestion = "Import the file into the project "
                                             "before exporting."});
        continue;
      }
      const auto bytes = readBytes(path);
      if (!bytes) {
        diagnostics.push_back({.severity = Severity::Error,
                               .code = "ASSET_PACKAGE_READ_FAILED",
                               .message = "Could not read asset file.",
                               .path = path.string()});
        continue;
      }
      const std::string relative =
          std::filesystem::relative(path, request.projectDirectory)
              .generic_string();
      files.emplace(relative, PackageFile{.path = relative,
                                          .size = bytes->size(),
                                          .hash = hashBytes(*bytes),
                                          .bytes = *bytes});
    }
  }
  if (hasErrors(diagnostics))
    return diagnostics;

  nlohmann::json fileEntries = nlohmann::json::array();
  for (const auto &[path, file] : files)
    fileEntries.push_back(
        {{"path", path}, {"size", file.size}, {"hash", file.hash}});
  const nlohmann::json header{{"format_version", 1},
                              {"package_type", "DemiAssetPackage"},
                              {"assets", std::move(assets)},
                              {"files", std::move(fileEntries)}};
  const std::string headerText = header.dump();
  std::error_code code;
  if (!request.outputPath.parent_path().empty())
    std::filesystem::create_directories(request.outputPath.parent_path(), code);
  std::ofstream output(request.outputPath, std::ios::binary);
  if (!output) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "ASSET_PACKAGE_WRITE_FAILED",
                           .message = "Could not create asset package.",
                           .path = request.outputPath.string()});
    return diagnostics;
  }
  output.write(Magic.data(), static_cast<std::streamsize>(Magic.size()));
  writeU64(output, headerText.size());
  output.write(headerText.data(),
               static_cast<std::streamsize>(headerText.size()));
  for (const auto &[path, file] : files) {
    (void)path;
    output.write(reinterpret_cast<const char *>(file.bytes.data()),
                 static_cast<std::streamsize>(file.bytes.size()));
  }
  return diagnostics;
}

Diagnostics importAssetPackage(const AssetPackageImportRequest &request,
                               std::vector<std::string> *conflicts) {
  Diagnostics diagnostics;
  auto package = readPackage(request.packagePath, diagnostics);
  if (!package)
    return diagnostics;
  const auto &[header, files] = *package;
  std::set<std::string> conflictSet;
  const AssetRegistry registry = loadAssetRegistry(request.projectDirectory);
  for (const auto &asset : header["assets"])
    if (findAsset(registry, asset.value("id", "")) != nullptr)
      conflictSet.insert("id:" + asset.value("id", ""));
  for (const auto &file : files)
    if (std::filesystem::exists(request.projectDirectory / file.path))
      conflictSet.insert("path:" + file.path);
  if (conflicts != nullptr)
    conflicts->assign(conflictSet.begin(), conflictSet.end());
  if (!conflictSet.empty() && !request.overwrite) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = "ASSET_PACKAGE_CONFLICT",
                           .message = "Asset package has " +
                                      std::to_string(conflictSet.size()) +
                                      " conflict(s).",
                           .path = request.packagePath.string(),
                           .suggestion = "Review the listed conflicts, choose "
                                         "different IDs, or pass --overwrite "
                                         "explicitly."});
    return diagnostics;
  }
  for (const auto &file : files) {
    const auto target = request.projectDirectory / file.path;
    std::error_code code;
    std::filesystem::create_directories(target.parent_path(), code);
    std::ofstream output(target, std::ios::binary);
    output.write(reinterpret_cast<const char *>(file.bytes.data()),
                 static_cast<std::streamsize>(file.bytes.size()));
    if (!output)
      diagnostics.push_back({.severity = Severity::Error,
                             .code = "ASSET_PACKAGE_EXTRACT_FAILED",
                             .message = "Could not extract package file.",
                             .path = target.string()});
  }
  return diagnostics;
}

} // namespace demi::assets
