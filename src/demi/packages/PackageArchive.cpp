#include "demi/packages/PackageArchive.h"

#include <mbedtls/sha256.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <vector>

namespace demi::packages {
namespace {

constexpr std::string_view Magic = "DEMI-PROJECT-PACKAGE\n";
constexpr std::uint64_t MaximumHeaderBytes = 8ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t MaximumFileBytes = 64ULL * 1024ULL * 1024ULL;
constexpr std::uint64_t MaximumArchiveBytes = 512ULL * 1024ULL * 1024ULL;
constexpr std::size_t MaximumFiles = 10000;

struct ArchiveFile {
  std::string path;
  std::uint64_t size = 0;
  std::string hash;
  std::vector<unsigned char> bytes;
};

void addError(Diagnostics &diagnostics, const std::filesystem::path &path,
              std::string code, std::string message) {
  diagnostics.push_back({.severity = Severity::Error,
                         .code = std::move(code),
                         .message = std::move(message),
                         .path = path.string()});
}

void writeU64(std::ostream &output, const std::uint64_t value) {
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

std::string encodeHash(const std::array<unsigned char, 32> &hash) {
  std::ostringstream output;
  output << "sha256:" << std::hex << std::setfill('0');
  for (const unsigned char byte : hash)
    output << std::setw(2) << static_cast<int>(byte);
  return output.str();
}

std::string sha256Bytes(const std::vector<unsigned char> &bytes) {
  std::array<unsigned char, 32> hash{};
  mbedtls_sha256(bytes.data(), bytes.size(), hash.data(), 0);
  return encodeHash(hash);
}

std::optional<std::vector<unsigned char>>
readFile(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input)
    return std::nullopt;
  const auto length = input.tellg();
  if (length < 0 || static_cast<std::uint64_t>(length) > MaximumFileBytes)
    return std::nullopt;
  std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
  input.seekg(0);
  input.read(reinterpret_cast<char *>(bytes.data()), length);
  return input ? std::make_optional(std::move(bytes)) : std::nullopt;
}

struct ReadArchiveResult {
  PackageManifest manifest;
  std::vector<ArchiveFile> files;
};

std::optional<ReadArchiveResult>
readArchive(const std::filesystem::path &path, Diagnostics &diagnostics,
            const bool includeBytes) {
  std::error_code sizeError;
  if (std::filesystem::file_size(path, sizeError) > MaximumArchiveBytes ||
      sizeError) {
    addError(diagnostics, path, "PACKAGE_ARCHIVE_TOO_LARGE",
             "Package archive is missing or exceeds the size limit.");
    return std::nullopt;
  }
  std::ifstream input(path, std::ios::binary);
  std::string magic(Magic.size(), '\0');
  input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  std::uint64_t headerSize = 0;
  if (!input || magic != Magic || !readU64(input, headerSize) ||
      headerSize > MaximumHeaderBytes) {
    addError(diagnostics, path, "PACKAGE_ARCHIVE_INVALID",
             "Package archive header is invalid.");
    return std::nullopt;
  }
  std::string headerText(static_cast<std::size_t>(headerSize), '\0');
  input.read(headerText.data(), static_cast<std::streamsize>(headerSize));
  nlohmann::json header;
  try {
    header = nlohmann::json::parse(headerText);
  } catch (const nlohmann::json::exception &exception) {
    addError(diagnostics, path, "PACKAGE_ARCHIVE_INVALID", exception.what());
    return std::nullopt;
  }
  if (header.value("format_version", 0) != 1 ||
      header.value("package_type", "") != "DemiProjectPackage" ||
      !header.contains("manifest") || !header["manifest"].is_object() ||
      !header.contains("files") || !header["files"].is_array() ||
      header["files"].size() > MaximumFiles) {
    addError(diagnostics, path, "PACKAGE_ARCHIVE_UNSUPPORTED",
             "Package archive format is unsupported.");
    return std::nullopt;
  }

  ManifestLoadResult loaded =
      parsePackageManifest(header["manifest"], path.string() + "#manifest");
  diagnostics.insert(diagnostics.end(), loaded.diagnostics.begin(),
                     loaded.diagnostics.end());
  if (!loaded.manifest)
    return std::nullopt;

  ReadArchiveResult result{.manifest = *loaded.manifest};
  std::set<std::string> paths;
  std::uint64_t totalBytes = 0;
  for (const auto &entry : header["files"]) {
    if (!entry.is_object()) {
      addError(diagnostics, path, "PACKAGE_ARCHIVE_INVALID",
               "Package archive contains an invalid file entry.");
      return std::nullopt;
    }
    ArchiveFile file{.path = entry.value("path", ""),
                     .size = entry.value("size", MaximumFileBytes + 1),
                     .hash = entry.value("hash", "")};
    if (!safePackageRelativePath(file.path) || file.size > MaximumFileBytes ||
        !file.hash.starts_with("sha256:") || !paths.insert(file.path).second ||
        totalBytes > MaximumArchiveBytes - file.size) {
      addError(diagnostics, path, "PACKAGE_ARCHIVE_UNSAFE",
               "Package archive contains an unsafe, duplicate, or oversized file: " +
                   file.path);
      return std::nullopt;
    }
    totalBytes += file.size;
    file.bytes.resize(static_cast<std::size_t>(file.size));
    input.read(reinterpret_cast<char *>(file.bytes.data()),
               static_cast<std::streamsize>(file.size));
    if (!input || sha256Bytes(file.bytes) != file.hash) {
      addError(diagnostics, path, "PACKAGE_ARCHIVE_CORRUPT",
               "Package file checksum failed: " + file.path);
      return std::nullopt;
    }
    if (!includeBytes)
      file.bytes.clear();
    result.files.push_back(std::move(file));
  }
  const std::set<std::string> declared(result.manifest.files.begin(),
                                       result.manifest.files.end());
  if (paths != declared) {
    addError(diagnostics, path, "PACKAGE_ARCHIVE_UNDECLARED_FILE",
             "Archive files do not exactly match the manifest files list.");
    return std::nullopt;
  }
  if (input.peek() != std::char_traits<char>::eof()) {
    addError(diagnostics, path, "PACKAGE_ARCHIVE_TRAILING_DATA",
             "Package archive contains trailing undeclared data.");
    return std::nullopt;
  }
  return result;
}

} // namespace

Diagnostics createPackageArchive(const std::filesystem::path &packageRoot,
                                 const std::filesystem::path &outputPath) {
  Diagnostics diagnostics;
  const auto loaded =
      loadPackageManifest(packageRoot / PackageManifestFilename);
  diagnostics = loaded.diagnostics;
  if (!loaded.manifest)
    return diagnostics;
  std::map<std::string, ArchiveFile> files;
  for (const auto &relative : loaded.manifest->files) {
    const auto source = packageRoot / relative;
    std::error_code statusError;
    const auto status = std::filesystem::symlink_status(source, statusError);
    if (statusError || !std::filesystem::is_regular_file(status) ||
        std::filesystem::is_symlink(status)) {
      addError(diagnostics, source, "PACKAGE_FILE_INVALID",
               "Declared package file is missing, not regular, or is a symlink.");
      continue;
    }
    const auto bytes = readFile(source);
    if (!bytes) {
      addError(diagnostics, source, "PACKAGE_FILE_READ_FAILED",
               "Declared package file could not be read or exceeds the limit.");
      continue;
    }
    files.emplace(relative,
                  ArchiveFile{.path = relative,
                              .size = bytes->size(),
                              .hash = sha256Bytes(*bytes),
                              .bytes = *bytes});
  }
  if (hasErrors(diagnostics))
    return diagnostics;

  nlohmann::json fileIndex = nlohmann::json::array();
  for (const auto &[unused, file] : files) {
    (void)unused;
    fileIndex.push_back(
        {{"path", file.path}, {"size", file.size}, {"hash", file.hash}});
  }
  const nlohmann::json header{{"format_version", 1},
                              {"package_type", "DemiProjectPackage"},
                              {"manifest", packageManifestJson(*loaded.manifest)},
                              {"files", std::move(fileIndex)}};
  const std::string headerText = header.dump();
  std::error_code directoryError;
  if (!outputPath.parent_path().empty())
    std::filesystem::create_directories(outputPath.parent_path(), directoryError);
  std::ofstream output(outputPath, std::ios::binary | std::ios::trunc);
  if (directoryError || !output) {
    addError(diagnostics, outputPath, "PACKAGE_ARCHIVE_WRITE_FAILED",
             "Could not create the package archive.");
    return diagnostics;
  }
  output.write(Magic.data(), static_cast<std::streamsize>(Magic.size()));
  writeU64(output, headerText.size());
  output.write(headerText.data(), static_cast<std::streamsize>(headerText.size()));
  for (const auto &[unused, file] : files) {
    (void)unused;
    output.write(reinterpret_cast<const char *>(file.bytes.data()),
                 static_cast<std::streamsize>(file.bytes.size()));
  }
  if (!output)
    addError(diagnostics, outputPath, "PACKAGE_ARCHIVE_WRITE_FAILED",
             "Writing the package archive failed.");
  return diagnostics;
}

std::optional<PackageArchiveInfo>
inspectPackageArchive(const std::filesystem::path &archivePath,
                      Diagnostics &diagnostics) {
  const auto archive = readArchive(archivePath, diagnostics, false);
  const auto hash = sha256File(archivePath);
  if (!archive || !hash)
    return std::nullopt;
  return PackageArchiveInfo{.manifest = archive->manifest, .archiveHash = *hash};
}

std::optional<PackageArchiveInfo>
extractPackageArchive(const std::filesystem::path &archivePath,
                      const std::filesystem::path &destination,
                      Diagnostics &diagnostics) {
  const auto archive = readArchive(archivePath, diagnostics, true);
  const auto hash = sha256File(archivePath);
  if (!archive || !hash)
    return std::nullopt;
  std::error_code error;
  std::filesystem::create_directories(destination, error);
  for (const auto &file : archive->files) {
    const auto target = destination / file.path;
    std::filesystem::create_directories(target.parent_path(), error);
    std::ofstream output(target, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char *>(file.bytes.data()),
                 static_cast<std::streamsize>(file.bytes.size()));
    if (!output || error) {
      addError(diagnostics, target, "PACKAGE_EXTRACT_FAILED",
               "Could not extract package file.");
      std::filesystem::remove_all(destination, error);
      return std::nullopt;
    }
  }
  std::ofstream manifestOutput(destination / PackageManifestFilename);
  manifestOutput << packageManifestJson(archive->manifest).dump(2) << '\n';
  if (!manifestOutput) {
    addError(diagnostics, destination, "PACKAGE_EXTRACT_FAILED",
             "Could not write installed package manifest.");
    std::filesystem::remove_all(destination, error);
    return std::nullopt;
  }
  return PackageArchiveInfo{.manifest = archive->manifest, .archiveHash = *hash};
}

} // namespace demi::packages
