#include "demi/packages/PackageInstaller.h"

#include <nlohmann/json.hpp>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <set>

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace demi::packages {
namespace {

void addError(Diagnostics &diagnostics, std::string code, std::string message,
              const std::filesystem::path &path = {}) {
  diagnostics.push_back({.severity = Severity::Error,
                         .code = std::move(code),
                         .message = std::move(message),
                         .path = path.string()});
}

bool writeJson(const std::filesystem::path &path, const nlohmann::json &value) {
  std::ofstream output(path, std::ios::trunc);
  output << value.dump(2) << '\n';
  return static_cast<bool>(output);
}

std::int64_t processId() {
#if defined(_WIN32)
  return 0;
#else
  return static_cast<std::int64_t>(::getpid());
#endif
}

bool processIsAlive(const std::int64_t pid) {
#if defined(_WIN32)
  return pid == 0;
#else
  if (pid <= 0)
    return false;
  if (::kill(static_cast<pid_t>(pid), 0) == 0)
    return true;
  return errno == EPERM;
#endif
}

std::filesystem::path defaultCacheDirectory() {
  if (const char *value = std::getenv("XDG_CACHE_HOME"); value != nullptr)
    return std::filesystem::path(value) / "demi" / "packages";
  if (const char *home = std::getenv("HOME"); home != nullptr)
    return std::filesystem::path(home) / ".cache" / "demi" / "packages";
  return std::filesystem::temp_directory_path() / "demi-package-cache";
}

class InstallLock {
public:
  explicit InstallLock(std::filesystem::path path) : path_(std::move(path)) {
    std::error_code error;
    acquired_ = std::filesystem::create_directory(path_, error);
    if (!acquired_ && std::filesystem::is_directory(path_)) {
      std::ifstream input(path_ / "owner.json");
      nlohmann::json owner;
      try {
        if (input)
          input >> owner;
      } catch (const nlohmann::json::exception &) {
        owner = nlohmann::json::object();
      }
      if (!processIsAlive(owner.value("pid", std::int64_t{-1}))) {
        std::filesystem::remove_all(path_, error);
        error.clear();
        acquired_ = std::filesystem::create_directory(path_, error);
      }
    }
    if (acquired_ && !writeJson(path_ / "owner.json", {{"pid", processId()}})) {
      std::filesystem::remove_all(path_, error);
      acquired_ = false;
    }
  }
  ~InstallLock() {
    if (acquired_) {
      std::error_code error;
      std::filesystem::remove_all(path_, error);
    }
  }
  [[nodiscard]] bool acquired() const { return acquired_; }

private:
  std::filesystem::path path_;
  bool acquired_ = false;
};

constexpr const char *TransactionFilename = "package-install-transaction.json";

void recoverInterruptedTransaction(const std::filesystem::path &state,
                                   Diagnostics &diagnostics) {
  const auto transactionPath = state / TransactionFilename;
  if (!std::filesystem::is_regular_file(transactionPath))
    return;
  std::ifstream input(transactionPath);
  nlohmann::json transaction;
  try {
    input >> transaction;
  } catch (const nlohmann::json::exception &) {
    addError(diagnostics, "PACKAGE_INSTALL_RECOVERY_FAILED",
             "The interrupted package transaction journal is invalid.",
             transactionPath);
    return;
  }

  const auto path = [&](const char *name) {
    return std::filesystem::path(transaction.value(name, ""));
  };
  std::error_code error;
  const auto staging = path("staging");
  const auto lockTemporary = path("lock_temporary");
  const auto projectTemporary = path("project_temporary");
  if (transaction.value("phase", "preparing") == "committing") {
    const auto installed = path("installed");
    const auto installedBackup = path("installed_backup");
    const auto lockPath = path("lock_path");
    const auto lockBackup = path("lock_backup");
    const auto projectPath = path("project_path");
    const auto projectBackup = path("project_backup");
    const bool hadInstalled = transaction.value("had_installed", false);
    const bool hadLock = transaction.value("had_lock", false);
    const bool hadProject = transaction.value("had_project", false);
    const bool replaceProject = transaction.value("replace_project", false);
    if (!hadInstalled || std::filesystem::exists(installedBackup))
      std::filesystem::remove_all(installed, error);
    if (!error && (!hadLock || std::filesystem::exists(lockBackup)))
      std::filesystem::remove(lockPath, error);
    if (!error && replaceProject &&
        (!hadProject || std::filesystem::exists(projectBackup)))
      std::filesystem::remove(projectPath, error);
    if (!error && hadInstalled && std::filesystem::exists(installedBackup))
      std::filesystem::rename(installedBackup, installed, error);
    if (!error && hadLock && std::filesystem::exists(lockBackup))
      std::filesystem::rename(lockBackup, lockPath, error);
    if (!error && replaceProject && hadProject &&
        std::filesystem::exists(projectBackup))
      std::filesystem::rename(projectBackup, projectPath, error);
  }
  if (!staging.empty())
    std::filesystem::remove_all(staging, error);
  if (!lockTemporary.empty())
    std::filesystem::remove(lockTemporary, error);
  if (!projectTemporary.empty())
    std::filesystem::remove(projectTemporary, error);
  if (error) {
    addError(diagnostics, "PACKAGE_INSTALL_RECOVERY_FAILED",
             "Could not restore an interrupted package transaction: " +
                 error.message(),
             transactionPath);
    return;
  }
  std::filesystem::remove(transactionPath, error);
}

class TransactionJournal {
public:
  explicit TransactionJournal(std::filesystem::path path)
      : path_(std::move(path)) {}
  ~TransactionJournal() { complete(); }

  [[nodiscard]] bool write(const nlohmann::json &value) const {
    return writeJson(path_, value);
  }

  void complete() {
    if (path_.empty())
      return;
    std::error_code error;
    std::filesystem::remove(path_, error);
    path_.clear();
  }

private:
  std::filesystem::path path_;
};

} // namespace

PackageLockLoadResult loadPackageLock(const std::filesystem::path &path) {
  PackageLockLoadResult result;
  std::ifstream input(path);
  if (!input) {
    addError(result.diagnostics, "PACKAGE_LOCK_READ_FAILED",
             "Could not read the package lock file.", path);
    return result;
  }
  nlohmann::json document;
  try {
    input >> document;
  } catch (const nlohmann::json::exception &exception) {
    addError(result.diagnostics, "PACKAGE_LOCK_INVALID_JSON", exception.what(),
             path);
    return result;
  }
  if (document.value("format_version", 0) != 1 ||
      !document.contains("packages") || !document["packages"].is_array()) {
    addError(result.diagnostics, "PACKAGE_LOCK_FORMAT_UNSUPPORTED",
             "Package lock requires format_version 1 and a packages array.",
             path);
    return result;
  }
  result.registry = document.value("registry", "");
  for (const auto &entry : document["packages"]) {
    if (!entry.is_object() || !entry.contains("manifest")) {
      addError(result.diagnostics, "PACKAGE_LOCK_ENTRY_INVALID",
               "Package lock contains an invalid entry.", path);
      continue;
    }
    auto loaded = parsePackageManifest(entry["manifest"], path.string());
    result.diagnostics.insert(result.diagnostics.end(),
                              loaded.diagnostics.begin(),
                              loaded.diagnostics.end());
    const std::string hash = entry.value("archive_hash", "");
    const std::string manifestHash = entry.value("manifest_hash", "");
    const std::string uri = entry.value("archive_uri", "");
    const std::string actualManifestHash =
        loaded.manifest
            ? sha256Text(packageManifestJson(*loaded.manifest).dump())
            : std::string{};
    if (!loaded.manifest || !hash.starts_with("sha256:") || uri.empty() ||
        manifestHash != actualManifestHash) {
      addError(result.diagnostics, "PACKAGE_LOCK_ENTRY_INVALID",
               "Locked package omits a valid manifest, archive hash, or URI.",
               path);
      continue;
    }
    if (!result.releases
             .emplace(loaded.manifest->name,
                      PackageRelease{.manifest = *loaded.manifest,
                                     .manifestHash = manifestHash,
                                     .archiveHash = hash,
                                     .archiveUri = uri,
                                     .yanked = entry.value("yanked", false)})
             .second)
      addError(result.diagnostics, "PACKAGE_LOCK_DUPLICATE",
               "Package lock contains a duplicate package: " +
                   loaded.manifest->name,
               path);
  }
  if (hasErrors(result.diagnostics))
    result.releases.clear();
  return result;
}

nlohmann::json
packageLockJson(const std::map<std::string, PackageRelease> &releases,
                const std::string &registry) {
  nlohmann::json packages = nlohmann::json::array();
  for (const auto &[unused, release] : releases) {
    (void)unused;
    packages.push_back(
        {{"manifest", packageManifestJson(release.manifest)},
         {"manifest_hash",
          release.manifestHash.empty()
              ? sha256Text(packageManifestJson(release.manifest).dump())
              : release.manifestHash},
         {"archive_hash", release.archiveHash},
         {"archive_uri", release.archiveUri},
         {"yanked", release.yanked}});
  }
  return {{"format_version", 1},
          {"registry", registry},
          {"packages", std::move(packages)}};
}

Diagnostics
installPackages(PackageRegistry &registry,
                const std::map<std::string, PackageRelease> &releases,
                const PackageInstallOptions &options) {
  Diagnostics diagnostics;
  const auto project = std::filesystem::absolute(options.projectDirectory);
  const auto state = project / ".demi";
  std::error_code error;
  std::filesystem::create_directories(state, error);
  InstallLock lock(state / "package-install.lock");
  if (!lock.acquired()) {
    addError(diagnostics, "PACKAGE_INSTALL_CONCURRENT",
             "Another package installation is active for this project.", state);
    return diagnostics;
  }
  recoverInterruptedTransaction(state, diagnostics);
  if (hasErrors(diagnostics))
    return diagnostics;
  const auto nonce = std::to_string(
      std::chrono::steady_clock::now().time_since_epoch().count());
  const auto staging = state / ("packages.staging-" + nonce);
  const auto installed = state / "packages";
  const auto installedBackup = state / ("packages.backup-" + nonce);
  const auto lockPath = project / PackageLockFilename;
  const auto lockBackup =
      project / (std::string(PackageLockFilename) + ".backup-" + nonce);
  const auto lockTemporary =
      project / (std::string(PackageLockFilename) + ".staging-" + nonce);
  const auto projectPath = project / "demi.project.json";
  const auto projectBackup = project / ("demi.project.json.backup-" + nonce);
  const auto projectTemporary =
      project / ("demi.project.json.staging-" + nonce);
  const bool hadInstalled = std::filesystem::exists(installed);
  const bool hadLock = std::filesystem::exists(lockPath);
  const bool hadProject = std::filesystem::exists(projectPath);
  const auto transactionPath = state / TransactionFilename;
  TransactionJournal journal(transactionPath);
  nlohmann::json transaction = {
      {"format_version", 1},
      {"phase", "preparing"},
      {"staging", staging.string()},
      {"installed", installed.string()},
      {"installed_backup", installedBackup.string()},
      {"lock_path", lockPath.string()},
      {"lock_backup", lockBackup.string()},
      {"lock_temporary", lockTemporary.string()},
      {"project_path", projectPath.string()},
      {"project_backup", projectBackup.string()},
      {"project_temporary", projectTemporary.string()},
      {"had_installed", hadInstalled},
      {"had_lock", hadLock},
      {"had_project", hadProject},
      {"replace_project", options.replacementProject.has_value()}};
  if (!journal.write(transaction)) {
    addError(diagnostics, "PACKAGE_INSTALL_JOURNAL_WRITE_FAILED",
             "Could not create the package transaction journal.",
             transactionPath);
    return diagnostics;
  }
  const auto cache = options.cacheDirectory.empty() ? defaultCacheDirectory()
                                                    : options.cacheDirectory;
  std::filesystem::create_directories(staging, error);
  std::filesystem::create_directories(cache, error);
  if (error) {
    addError(diagnostics, "PACKAGE_INSTALL_PREPARE_FAILED", error.message(),
             staging);
    return diagnostics;
  }

  std::set<std::string> publicModules;
  for (const auto &[name, release] : releases) {
    for (const auto &module : release.manifest.publicModules)
      if (!publicModules.insert(module).second)
        addError(diagnostics, "PACKAGE_PUBLIC_MODULE_CONFLICT",
                 "Multiple packages export Lua module: " + module, name);
  }
  if (hasErrors(diagnostics)) {
    std::filesystem::remove_all(staging, error);
    return diagnostics;
  }

  for (const auto &[name, release] : releases) {
    const std::string cacheName = release.archiveHash.substr(7) + ".demipkg";
    const auto cached = cache / cacheName;
    if (!std::filesystem::exists(cached)) {
      if (options.offline) {
        addError(diagnostics, "PACKAGE_OFFLINE_CACHE_MISS",
                 "Offline cache is missing " + name + "@" +
                     release.manifest.version.string() + ".",
                 cached);
        continue;
      }
      const auto temporary = cache / (cacheName + ".download-" + nonce);
      if (!registry.download(release, temporary, diagnostics))
        continue;
      const auto actual = sha256File(temporary);
      if (!actual || *actual != release.archiveHash) {
        std::filesystem::remove(temporary, error);
        addError(diagnostics, "PACKAGE_ARCHIVE_HASH_MISMATCH",
                 "Downloaded archive hash does not match registry metadata.",
                 temporary);
        continue;
      }
      std::filesystem::rename(temporary, cached, error);
      if (error && !std::filesystem::exists(cached)) {
        addError(diagnostics, "PACKAGE_CACHE_COMMIT_FAILED", error.message(),
                 cached);
        continue;
      }
    }
    const auto actual = sha256File(cached);
    if (!actual || *actual != release.archiveHash) {
      addError(diagnostics, "PACKAGE_CACHE_CORRUPT",
               "Cached archive failed verification.", cached);
      continue;
    }
    const auto destination = staging / name;
    const auto extracted =
        extractPackageArchive(cached, destination, diagnostics);
    const std::string expectedManifestHash =
        release.manifestHash.empty()
            ? sha256Text(packageManifestJson(release.manifest).dump())
            : release.manifestHash;
    if (!extracted || extracted->manifest.name != name ||
        extracted->manifest.version != release.manifest.version ||
        sha256Text(packageManifestJson(extracted->manifest).dump()) !=
            expectedManifestHash) {
      if (extracted)
        addError(diagnostics, "PACKAGE_ARCHIVE_IDENTITY_MISMATCH",
                 "Archive manifest differs from resolved metadata.", cached);
      continue;
    }
  }
  if (hasErrors(diagnostics)) {
    std::filesystem::remove_all(staging, error);
    return diagnostics;
  }
  if (options.validateStaging) {
    auto validation = options.validateStaging(staging);
    diagnostics.insert(diagnostics.end(), validation.begin(), validation.end());
    if (hasErrors(diagnostics)) {
      std::filesystem::remove_all(staging, error);
      return diagnostics;
    }
  }
  if (options.dryRun) {
    std::filesystem::remove_all(staging, error);
    return diagnostics;
  }

  if (!writeJson(lockTemporary, packageLockJson(releases, registry.source()))) {
    addError(diagnostics, "PACKAGE_LOCK_WRITE_FAILED",
             "Could not stage the package lock.", lockTemporary);
    std::filesystem::remove_all(staging, error);
    return diagnostics;
  }
  if (options.replacementProject &&
      !writeJson(projectTemporary, *options.replacementProject)) {
    addError(diagnostics, "PACKAGE_PROJECT_WRITE_FAILED",
             "Could not stage project dependency changes.", projectTemporary);
    std::filesystem::remove_all(staging, error);
    std::filesystem::remove(lockTemporary, error);
    return diagnostics;
  }

  transaction["phase"] = "committing";
  if (!journal.write(transaction)) {
    addError(diagnostics, "PACKAGE_INSTALL_JOURNAL_WRITE_FAILED",
             "Could not record the package commit transaction.",
             transactionPath);
    std::filesystem::remove_all(staging, error);
    std::filesystem::remove(lockTemporary, error);
    std::filesystem::remove(projectTemporary, error);
    return diagnostics;
  }
  if (hadInstalled)
    std::filesystem::rename(installed, installedBackup, error);
  if (!error && hadLock)
    std::filesystem::rename(lockPath, lockBackup, error);
  if (!error && options.replacementProject && hadProject)
    std::filesystem::rename(projectPath, projectBackup, error);
  if (!error)
    std::filesystem::rename(staging, installed, error);
  if (!error)
    std::filesystem::rename(lockTemporary, lockPath, error);
  if (!error && options.replacementProject)
    std::filesystem::rename(projectTemporary, projectPath, error);
  if (error) {
    std::error_code rollbackError;
    std::filesystem::remove_all(installed, rollbackError);
    std::filesystem::remove(lockPath, rollbackError);
    if (options.replacementProject)
      std::filesystem::remove(projectPath, rollbackError);
    if (hadInstalled && std::filesystem::exists(installedBackup))
      std::filesystem::rename(installedBackup, installed, rollbackError);
    if (hadLock && std::filesystem::exists(lockBackup))
      std::filesystem::rename(lockBackup, lockPath, rollbackError);
    if (options.replacementProject && hadProject &&
        std::filesystem::exists(projectBackup))
      std::filesystem::rename(projectBackup, projectPath, rollbackError);
    std::filesystem::remove_all(staging, rollbackError);
    std::filesystem::remove(lockTemporary, rollbackError);
    std::filesystem::remove(projectTemporary, rollbackError);
    addError(diagnostics, "PACKAGE_INSTALL_COMMIT_FAILED",
             "Atomic package installation commit failed; the previous package "
             "tree, lock, and project manifest were restored.",
             project);
    if (rollbackError)
      addError(diagnostics, "PACKAGE_INSTALL_ROLLBACK_FAILED",
               "Package rollback also failed: " + rollbackError.message(),
               project);
    journal.complete();
    return diagnostics;
  }
  journal.complete();
  std::filesystem::remove_all(installedBackup, error);
  std::filesystem::remove(lockBackup, error);
  std::filesystem::remove(projectBackup, error);
  return diagnostics;
}

} // namespace demi::packages
