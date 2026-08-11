#include "demi/packages/PackageRegistry.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

namespace demi::packages {
namespace {

class CurlRuntime final {
public:
  CurlRuntime()
      : initialized_(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK) {}
  ~CurlRuntime() {
    if (initialized_)
      curl_global_cleanup();
  }
  [[nodiscard]] bool initialized() const { return initialized_; }

private:
  bool initialized_ = false;
};

CurlRuntime &curlRuntime() {
  static CurlRuntime runtime;
  return runtime;
}

void addError(Diagnostics &diagnostics, std::string code, std::string message,
              const std::string &path = {}) {
  diagnostics.push_back({.severity = Severity::Error,
                         .code = std::move(code),
                         .message = std::move(message),
                         .path = path});
}

class DirectoryPackageRegistry final : public PackageRegistry {
public:
  DirectoryPackageRegistry(std::filesystem::path root, std::string source)
      : root_(std::filesystem::absolute(std::move(root))),
        source_(std::move(source)) {}

  std::vector<PackageRelease> releases(const std::string &name,
                                       Diagnostics &diagnostics) override {
    std::vector<PackageRelease> result;
    const auto packageRoot = root_ / "packages" / name;
    std::error_code error;
    if (std::filesystem::is_directory(packageRoot, error)) {
      for (const auto &entry :
           std::filesystem::directory_iterator(packageRoot)) {
        if (!entry.is_directory())
          continue;
        const auto manifestPath = entry.path() / PackageManifestFilename;
        const auto archivePath = entry.path() / "package.demipkg";
        auto loaded = loadPackageManifest(manifestPath);
        diagnostics.insert(diagnostics.end(), loaded.diagnostics.begin(),
                           loaded.diagnostics.end());
        const auto hash = sha256File(archivePath);
        if (!loaded.manifest || !hash)
          continue;
        if (loaded.manifest->name != name ||
            loaded.manifest->version.string() !=
                entry.path().filename().string()) {
          addError(diagnostics, "PACKAGE_REGISTRY_METADATA_MISMATCH",
                   "Registry directory does not match its package manifest.",
                   entry.path().string());
          continue;
        }
        result.push_back(
            {.manifest = *loaded.manifest,
             .manifestHash =
                 sha256Text(packageManifestJson(*loaded.manifest).dump()),
             .archiveHash = *hash,
             .archiveUri =
                 "package://" + name + "/" + loaded.manifest->version.string(),
             .yanked = std::filesystem::exists(entry.path() / "YANKED")});
      }
    }
    const auto sourceRoot = root_ / "sources" / name;
    const auto sourceManifest = sourceRoot / PackageManifestFilename;
    if (std::filesystem::exists(sourceManifest)) {
      auto loaded = loadPackageManifest(sourceManifest);
      diagnostics.insert(diagnostics.end(), loaded.diagnostics.begin(),
                         loaded.diagnostics.end());
      if (loaded.manifest && loaded.manifest->name == name) {
        const auto version = loaded.manifest->version.string();
        const auto cached =
            root_ / ".registry-cache" / name / version / "package.demipkg";
        std::filesystem::create_directories(cached.parent_path(), error);
        auto archiveDiagnostics = createPackageArchive(sourceRoot, cached);
        diagnostics.insert(diagnostics.end(), archiveDiagnostics.begin(),
                           archiveDiagnostics.end());
        const auto hash = sha256File(cached);
        if (hash)
          result.push_back({.manifest = *loaded.manifest,
                            .manifestHash = sha256Text(
                                packageManifestJson(*loaded.manifest).dump()),
                            .archiveHash = *hash,
                            .archiveUri = "package://" + name + "/" + version,
                            .yanked = false});
      } else if (loaded.manifest) {
        addError(diagnostics, "PACKAGE_REGISTRY_METADATA_MISMATCH",
                 "Source registry directory does not match its manifest name.",
                 sourceRoot.string());
      }
    }
    return result;
  }

  bool download(const PackageRelease &release,
                const std::filesystem::path &destination,
                Diagnostics &diagnostics) override {
    const auto name = release.manifest.name;
    const auto version = release.manifest.version.string();
    auto source = root_ / "packages" / name / version / "package.demipkg";
    if (!std::filesystem::is_regular_file(source)) {
      source = root_ / ".registry-cache" / name / version / "package.demipkg";
      const auto sourceRoot = root_ / "sources" / name;
      if (std::filesystem::is_directory(sourceRoot)) {
        auto archiveDiagnostics = createPackageArchive(sourceRoot, source);
        diagnostics.insert(diagnostics.end(), archiveDiagnostics.begin(),
                           archiveDiagnostics.end());
      }
    }
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    std::filesystem::copy_file(
        source, destination, std::filesystem::copy_options::overwrite_existing,
        error);
    if (error) {
      addError(diagnostics, "PACKAGE_DOWNLOAD_FAILED", error.message(),
               source.string());
      return false;
    }
    return true;
  }

  bool publish(const std::filesystem::path &archivePath,
               const PackageArchiveInfo &archive,
               Diagnostics &diagnostics) override {
    const auto destination = root_ / "packages" / archive.manifest.name /
                             archive.manifest.version.string();
    if (std::filesystem::exists(destination)) {
      addError(diagnostics, "PACKAGE_VERSION_IMMUTABLE",
               "Package version already exists and cannot be replaced.",
               destination.string());
      return false;
    }
    const auto staging =
        std::filesystem::path(destination.string() + ".staging");
    std::error_code error;
    std::filesystem::remove_all(staging, error);
    std::filesystem::create_directories(staging, error);
    std::filesystem::copy_file(archivePath, staging / "package.demipkg",
                               std::filesystem::copy_options::none, error);
    std::ofstream manifest(staging / PackageManifestFilename);
    manifest << packageManifestJson(archive.manifest).dump(2) << '\n';
    if (error || !manifest) {
      std::filesystem::remove_all(staging, error);
      addError(diagnostics, "PACKAGE_PUBLISH_FAILED",
               "Could not stage the package in the directory registry.",
               destination.string());
      return false;
    }
    std::filesystem::rename(staging, destination, error);
    if (error) {
      std::filesystem::remove_all(staging, error);
      addError(diagnostics, "PACKAGE_PUBLISH_FAILED", error.message(),
               destination.string());
      return false;
    }
    return true;
  }

  std::string source() const override { return source_; }

private:
  std::filesystem::path root_;
  std::string source_;
};

size_t appendToString(char *data, size_t size, size_t count, void *target) {
  static_cast<std::string *>(target)->append(data, size * count);
  return size * count;
}

size_t appendToFile(char *data, size_t size, size_t count, void *target) {
  auto &output = *static_cast<std::ofstream *>(target);
  output.write(data, static_cast<std::streamsize>(size * count));
  return output ? size * count : 0;
}

std::string urlEncode(CURL *curl, const std::string &value) {
  char *encoded = curl_easy_escape(curl, value.c_str(), value.size());
  if (encoded == nullptr)
    return {};
  std::string result(encoded);
  curl_free(encoded);
  return result;
}

class HttpPackageRegistry final : public PackageRegistry {
public:
  explicit HttpPackageRegistry(std::string baseUrl)
      : baseUrl_(std::move(baseUrl)) {
    while (baseUrl_.ends_with('/'))
      baseUrl_.pop_back();
  }

  std::vector<PackageRelease> releases(const std::string &name,
                                       Diagnostics &diagnostics) override {
    CURL *curl = curl_easy_init();
    if (curl == nullptr) {
      addError(diagnostics, "PACKAGE_HTTP_INIT_FAILED",
               "Could not initialize the HTTP registry client.");
      return {};
    }
    const std::string url = baseUrl_ + "/v1/packages/" + urlEncode(curl, name);
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    const CURLcode status = curl_easy_perform(curl);
    long httpStatus = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
    curl_easy_cleanup(curl);
    if (status != CURLE_OK || (httpStatus != 200 && httpStatus != 404)) {
      addError(diagnostics, "PACKAGE_REGISTRY_REQUEST_FAILED",
               "Registry request failed for " + url + ".", url);
      return {};
    }
    if (httpStatus == 404)
      return {};
    nlohmann::json document;
    try {
      document = nlohmann::json::parse(response);
    } catch (const nlohmann::json::exception &exception) {
      addError(diagnostics, "PACKAGE_REGISTRY_RESPONSE_INVALID",
               exception.what(), url);
      return {};
    }
    if (document.value("format_version", 0) != 1 ||
        !document.contains("releases") || !document["releases"].is_array()) {
      addError(diagnostics, "PACKAGE_REGISTRY_RESPONSE_INVALID",
               "Registry response does not use format_version 1.", url);
      return {};
    }
    std::vector<PackageRelease> result;
    for (const auto &entry : document["releases"]) {
      if (!entry.is_object() || !entry.contains("manifest"))
        continue;
      auto loaded = parsePackageManifest(entry["manifest"], url);
      diagnostics.insert(diagnostics.end(), loaded.diagnostics.begin(),
                         loaded.diagnostics.end());
      if (!loaded.manifest || loaded.manifest->name != name)
        continue;
      std::string archiveUrl = entry.value("archive_url", "");
      if (archiveUrl.starts_with('/'))
        archiveUrl = baseUrl_ + archiveUrl;
      const std::string hash = entry.value("archive_hash", "");
      const std::string manifestHash = entry.value("manifest_hash", "");
      const std::string actualManifestHash =
          sha256Text(packageManifestJson(*loaded.manifest).dump());
      if (archiveUrl.empty() || !hash.starts_with("sha256:") ||
          (!manifestHash.empty() && manifestHash != actualManifestHash)) {
        addError(
            diagnostics, "PACKAGE_REGISTRY_RESPONSE_INVALID",
            "Registry release has invalid URL, archive hash, or manifest hash.",
            url);
        continue;
      }
      result.push_back({.manifest = *loaded.manifest,
                        .manifestHash = actualManifestHash,
                        .archiveHash = hash,
                        .archiveUri = archiveUrl,
                        .yanked = entry.value("yanked", false)});
    }
    return result;
  }

  bool download(const PackageRelease &release,
                const std::filesystem::path &destination,
                Diagnostics &diagnostics) override {
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    CURL *curl = curl_easy_init();
    if (error || !output || curl == nullptr) {
      addError(diagnostics, "PACKAGE_DOWNLOAD_FAILED",
               "Could not prepare the package download.", release.archiveUri);
      if (curl != nullptr)
        curl_easy_cleanup(curl);
      return false;
    }
    curl_easy_setopt(curl, CURLOPT_URL, release.archiveUri.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToFile);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
    const CURLcode status = curl_easy_perform(curl);
    long httpStatus = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
    curl_easy_cleanup(curl);
    output.close();
    if (status != CURLE_OK || httpStatus != 200) {
      std::filesystem::remove(destination, error);
      addError(diagnostics, "PACKAGE_DOWNLOAD_FAILED",
               "Package archive request failed.", release.archiveUri);
      return false;
    }
    return true;
  }

  bool publish(const std::filesystem::path &archivePath,
               const PackageArchiveInfo &archive,
               Diagnostics &diagnostics) override {
    CURL *curl = curl_easy_init();
    if (curl == nullptr) {
      addError(diagnostics, "PACKAGE_HTTP_INIT_FAILED",
               "Could not initialize the HTTP registry client.");
      return false;
    }
    const std::string name = urlEncode(curl, archive.manifest.name);
    const std::string version =
        urlEncode(curl, archive.manifest.version.string());
    const std::string base = baseUrl_ + "/v1/packages/" + name + "/" + version;
    const auto request = [&](const std::string &url, const std::string &body,
                             const char *contentType) {
      struct curl_slist *headers = nullptr;
      const std::string contentHeader =
          std::string("Content-Type: ") + contentType;
      headers = curl_slist_append(headers, contentHeader.c_str());
      curl_easy_reset(curl);
      curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.data());
      curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE_LARGE,
                       static_cast<curl_off_t>(body.size()));
      std::string response;
      curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, appendToString);
      curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
      curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);
      const CURLcode result = curl_easy_perform(curl);
      long httpStatus = 0;
      curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
      curl_slist_free_all(headers);
      return result == CURLE_OK && httpStatus == 201;
    };
    const std::string manifest = packageManifestJson(archive.manifest).dump();
    if (!request(base + "/manifest", manifest, "application/json")) {
      curl_easy_cleanup(curl);
      addError(diagnostics, "PACKAGE_PUBLISH_FAILED",
               "Registry rejected the package manifest.", base);
      return false;
    }
    std::ifstream archiveInput(archivePath, std::ios::binary);
    std::ostringstream archiveBytes;
    archiveBytes << archiveInput.rdbuf();
    const bool uploaded =
        archiveInput.good() && request(base + "/archive", archiveBytes.str(),
                                       "application/octet-stream");
    curl_easy_cleanup(curl);
    if (!uploaded) {
      addError(diagnostics, "PACKAGE_PUBLISH_FAILED",
               "Registry rejected the package archive.", base);
      return false;
    }
    return true;
  }

  std::string source() const override { return baseUrl_; }

private:
  std::string baseUrl_;
};

} // namespace

std::unique_ptr<PackageRegistry> makePackageRegistry(const std::string &source,
                                                     Diagnostics &diagnostics) {
  return makePackageRegistry(source, diagnostics,
                             std::filesystem::current_path());
}

std::unique_ptr<PackageRegistry>
makePackageRegistry(const std::string &source, Diagnostics &diagnostics,
                    const std::filesystem::path &baseDirectory) {
  if (source.starts_with("http://") || source.starts_with("https://")) {
    if (!curlRuntime().initialized()) {
      addError(diagnostics, "PACKAGE_HTTP_INIT_FAILED",
               "Could not initialize libcurl for the package registry.");
      return nullptr;
    }
    return std::make_unique<HttpPackageRegistry>(source);
  }
  std::filesystem::path path = source;
  if (source.starts_with("file://"))
    path = source.substr(7);
  if (path.empty()) {
    addError(diagnostics, "PACKAGE_REGISTRY_INVALID",
             "Package registry source cannot be empty.");
    return nullptr;
  }
  if (path.is_relative())
    path = baseDirectory / path;
  return std::make_unique<DirectoryPackageRegistry>(std::move(path), source);
}

} // namespace demi::packages
