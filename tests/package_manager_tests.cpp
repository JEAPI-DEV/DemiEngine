#include "demi/packages/PackageArchive.h"
#include "demi/packages/PackageInstaller.h"
#include "demi/packages/PackageRegistry.h"
#include "demi/packages/PackageResolver.h"
#include "demi/packages/SemanticVersion.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <map>

namespace {

using namespace demi;
using namespace demi::packages;

void write(const std::filesystem::path &path, const std::string &text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << text;
  assert(output);
}

std::filesystem::path makeRoot() {
  const auto root =
      std::filesystem::temp_directory_path() / "demi-package-manager-tests";
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::filesystem::create_directories(root);
  return root;
}

PackageManifest manifest(const std::string &name, const std::string &version,
                         std::vector<PackageDependency> dependencies = {}) {
  std::string modulePath = "scripts/";
  for (const char character : name)
    modulePath += character == '.' ? '/' : character;
  modulePath += ".lua";
  return {.name = name,
          .version = *SemanticVersion::parse(version),
          .engineVersion = *VersionConstraint::parse("^0.1.0"),
          .dependencies = std::move(dependencies),
          .publicModules = {name},
          .files = {std::move(modulePath)},
          .exportedEvents = {}};
}

class MemoryCatalog final : public PackageCatalog {
public:
  std::map<std::string, std::vector<PackageRelease>> values;
  std::vector<PackageRelease> releases(const std::string &name,
                                       Diagnostics &) override {
    return values[name];
  }
};

void testSemanticVersions() {
  assert(SemanticVersion::parse("1.2.3"));
  assert(!SemanticVersion::parse("1.2"));
  assert(!SemanticVersion::parse("01.2.3"));
  assert(!SemanticVersion::parse("1.2.3-alpha.01"));
  assert(!SemanticVersion::parse("1.2.3+"));
  assert(*SemanticVersion::parse("1.2.3") >
         *SemanticVersion::parse("1.2.3-beta"));
  assert(*SemanticVersion::parse("1.2.3-beta.11") >
         *SemanticVersion::parse("1.2.3-beta.2"));
  assert(VersionConstraint::parse("^1.2.3")->accepts(
      *SemanticVersion::parse("1.9.0")));
  assert(!VersionConstraint::parse("^1.2.3")->accepts(
      *SemanticVersion::parse("2.0.0")));
  assert(VersionConstraint::parse(">=1.0.0,<2.0.0")
             ->accepts(*SemanticVersion::parse("1.5.0")));
}

void testPackageNames() {
  assert(validPackageName("demi.gameplay.health"));
  assert(validPackageName("local_package-2"));
  assert(!validPackageName("demi..health"));
  assert(!validPackageName(".demi.health"));
  assert(!validPackageName("demi.health."));
  assert(!validPackageName("Demi.Health"));
}

void testResolverBacktracksAndRejectsCycles() {
  MemoryCatalog catalog;
  const auto b1 = manifest("demi.b", "1.0.0");
  const auto b2 = manifest("demi.b", "2.0.0");
  const auto a1 = manifest(
      "demi.a", "1.0.0",
      {{.name = "demi.b", .constraint = *VersionConstraint::parse("^1.0.0")}});
  const auto a2 = manifest(
      "demi.a", "2.0.0",
      {{.name = "demi.b", .constraint = *VersionConstraint::parse("^2.0.0")}});
  const auto c1 = manifest(
      "demi.c", "1.0.0",
      {{.name = "demi.b", .constraint = *VersionConstraint::parse("^1.0.0")}});
  catalog.values["demi.a"] = {{.manifest = a2}, {.manifest = a1}};
  catalog.values["demi.b"] = {{.manifest = b2}, {.manifest = b1}};
  catalog.values["demi.c"] = {{.manifest = c1}};
  auto resolution =
      resolvePackages(catalog,
                      {{.name = "demi.a",
                        .constraint = *VersionConstraint::parse("*"),
                        .requestedBy = "project"},
                       {.name = "demi.c",
                        .constraint = *VersionConstraint::parse("*"),
                        .requestedBy = "project"}},
                      *SemanticVersion::parse("0.1.0"));
  assert(!hasErrors(resolution.diagnostics));
  assert(resolution.selected.at("demi.a").manifest.version ==
         *SemanticVersion::parse("1.0.0"));

  const auto cycleA = manifest(
      "cycle.a", "1.0.0",
      {{.name = "cycle.b", .constraint = *VersionConstraint::parse("*")}});
  const auto cycleB = manifest(
      "cycle.b", "1.0.0",
      {{.name = "cycle.a", .constraint = *VersionConstraint::parse("*")}});
  catalog.values["cycle.a"] = {{.manifest = cycleA}};
  catalog.values["cycle.b"] = {{.manifest = cycleB}};
  resolution = resolvePackages(catalog,
                               {{.name = "cycle.a",
                                 .constraint = *VersionConstraint::parse("*"),
                                 .requestedBy = "project"}},
                               *SemanticVersion::parse("0.1.0"));
  assert(hasErrors(resolution.diagnostics));
  assert(resolution.selected.empty());

  catalog.values["demi.yanked"] = {
      {.manifest = manifest("demi.yanked", "2.0.0"), .yanked = true},
      {.manifest = manifest("demi.yanked", "1.0.0")}};
  resolution = resolvePackages(catalog,
                               {{.name = "demi.yanked",
                                 .constraint = *VersionConstraint::parse("*"),
                                 .requestedBy = "project"}},
                               *SemanticVersion::parse("0.1.0"));
  assert(!hasErrors(resolution.diagnostics));
  assert(resolution.selected.at("demi.yanked").manifest.version ==
         *SemanticVersion::parse("1.0.0"));
}

std::filesystem::path makePackage(const std::filesystem::path &root,
                                  const std::string &name,
                                  const std::string &version) {
  const auto package = root / (name + "-source");
  const auto data = manifest(name, version);
  write(package / PackageManifestFilename,
        packageManifestJson(data).dump(2) + "\n");
  write(package / data.files.front(), "return { value = 42 }\n");
  const auto archive = root / (name + "-" + version + ".demipkg");
  const Diagnostics diagnostics = createPackageArchive(package, archive);
  assert(!hasErrors(diagnostics));
  return archive;
}

void testArchiveRegistryInstallAndFailurePreservation(
    const std::filesystem::path &root) {
  const auto archive = makePackage(root, "demi.health", "1.0.0");
  Diagnostics diagnostics;
  const auto info = inspectPackageArchive(archive, diagnostics);
  assert(info && !hasErrors(diagnostics));

  const auto registryRoot = root / "registry";
  auto registry = makePackageRegistry(registryRoot.string(), diagnostics);
  assert(registry && registry->publish(archive, *info, diagnostics));
  assert(!hasErrors(diagnostics));
  auto releases = registry->releases("demi.health", diagnostics);
  assert(releases.size() == 1);

  const auto project = root / "project";
  write(
      project / "demi.project.json",
      R"({"format_version":1,"name":"fixture","scenes":[],"packages":{"demi.health":"^1.0.0"}})");
  const auto cache = root / "cache";
  diagnostics =
      installPackages(*registry, {{"demi.health", releases.front()}},
                      {.projectDirectory = project, .cacheDirectory = cache});
  assert(!hasErrors(diagnostics));
  assert(std::filesystem::exists(
      project / ".demi/packages/demi.health/scripts/demi/health.lua"));
  assert(std::filesystem::exists(project / PackageLockFilename));

  diagnostics = installPackages(
      *registry, {{"demi.health", releases.front()}},
      {.projectDirectory = project, .cacheDirectory = cache, .offline = true});
  assert(!hasErrors(diagnostics));

  auto mismatchedManifest = releases.front();
  mismatchedManifest.manifest.exportedEvents.push_back("not-in-the-archive");
  mismatchedManifest.manifestHash =
      sha256Text(packageManifestJson(mismatchedManifest.manifest).dump());
  diagnostics =
      installPackages(*registry, {{"demi.health", mismatchedManifest}},
                      {.projectDirectory = project, .cacheDirectory = cache});
  assert(hasErrors(diagnostics));
  assert(std::ranges::any_of(diagnostics, [](const Diagnostic &diagnostic) {
    return diagnostic.code == "PACKAGE_ARCHIVE_IDENTITY_MISMATCH";
  }));

  write(project / ".demi/packages/keep.txt", "previous-install\n");
  diagnostics = installPackages(
      *registry, {{"demi.health", releases.front()}},
      {.projectDirectory = project,
       .cacheDirectory = cache,
       .validateStaging = [](const std::filesystem::path &) {
         return Diagnostics{{.severity = Severity::Error,
                             .code = "FIXTURE_VALIDATION_FAILURE",
                             .message = "injected staging validation failure"}};
       }});
  assert(hasErrors(diagnostics));
  assert(std::filesystem::exists(project / ".demi/packages/keep.txt"));

  std::filesystem::create_directories(project / ".demi/package-install.lock");
  write(project / ".demi/package-install.lock/owner.json", R"({"pid":1})");
  diagnostics =
      installPackages(*registry, {{"demi.health", releases.front()}},
                      {.projectDirectory = project, .cacheDirectory = cache});
  assert(hasErrors(diagnostics));
  assert(std::ranges::any_of(diagnostics, [](const Diagnostic &diagnostic) {
    return diagnostic.code == "PACKAGE_INSTALL_CONCURRENT";
  }));
  std::filesystem::remove_all(project / ".demi/package-install.lock");

  const auto interruptedInstalled = project / ".demi/packages";
  const auto interruptedInstalledBackup =
      project / ".demi/packages.backup-interrupted";
  const auto interruptedLock = project / PackageLockFilename;
  const auto interruptedLockBackup =
      project / "demi.packages.lock.json.backup-interrupted";
  const auto interruptedStaging =
      project / ".demi/packages.staging-interrupted";
  std::filesystem::rename(interruptedInstalled, interruptedInstalledBackup);
  std::filesystem::rename(interruptedLock, interruptedLockBackup);
  write(interruptedInstalled / "new.txt", "partial-install\n");
  write(interruptedLock, "{}\n");
  write(interruptedStaging / "partial.txt", "partial-staging\n");
  write(project / ".demi/package-install.lock/owner.json",
        R"({"pid":999999999})");
  write(
      project / ".demi/package-install-transaction.json",
      nlohmann::json{{"format_version", 1},
                     {"phase", "committing"},
                     {"staging", interruptedStaging.string()},
                     {"installed", interruptedInstalled.string()},
                     {"installed_backup", interruptedInstalledBackup.string()},
                     {"lock_path", interruptedLock.string()},
                     {"lock_backup", interruptedLockBackup.string()},
                     {"lock_temporary", ""},
                     {"project_path", (project / "demi.project.json").string()},
                     {"project_backup", ""},
                     {"project_temporary", ""},
                     {"had_installed", true},
                     {"had_lock", true},
                     {"had_project", true},
                     {"replace_project", false}}
          .dump(2));
  diagnostics = installPackages(
      *registry, {{"demi.health", releases.front()}},
      {.projectDirectory = project,
       .cacheDirectory = cache,
       .validateStaging = [](const std::filesystem::path &) {
         return Diagnostics{{.severity = Severity::Error,
                             .code = "FIXTURE_STOP_AFTER_RECOVERY",
                             .message = "stop after recovery"}};
       }});
  assert(hasErrors(diagnostics));
  assert(std::filesystem::exists(interruptedInstalled / "keep.txt"));
  assert(!std::filesystem::exists(interruptedInstalled / "new.txt"));
  assert(!std::filesystem::exists(interruptedStaging));
  assert(!std::filesystem::exists(project /
                                  ".demi/package-install-transaction.json"));

  auto conflicting = releases.front();
  conflicting.manifest.name = "demi.other";
  diagnostics = installPackages(
      *registry,
      {{"demi.health", releases.front()}, {"demi.other", conflicting}},
      {.projectDirectory = project, .cacheDirectory = cache});
  assert(hasErrors(diagnostics));
  assert(std::ranges::any_of(diagnostics, [](const Diagnostic &diagnostic) {
    return diagnostic.code == "PACKAGE_PUBLIC_MODULE_CONFLICT";
  }));
  assert(std::filesystem::exists(project / ".demi/packages/keep.txt"));

  const auto cached =
      cache / (releases.front().archiveHash.substr(7) + ".demipkg");
  write(cached, "corrupt");
  diagnostics = installPackages(
      *registry, {{"demi.health", releases.front()}},
      {.projectDirectory = project, .cacheDirectory = cache, .offline = true});
  assert(hasErrors(diagnostics));
  assert(std::filesystem::exists(project / ".demi/packages/keep.txt"));

  std::filesystem::remove(cached);
  diagnostics = installPackages(
      *registry, {{"demi.health", releases.front()}},
      {.projectDirectory = project, .cacheDirectory = cache, .offline = true});
  assert(hasErrors(diagnostics));
  assert(std::ranges::any_of(diagnostics, [](const Diagnostic &diagnostic) {
    return diagnostic.code == "PACKAGE_OFFLINE_CACHE_MISS";
  }));
}

void testUndeclaredAndCorruptArchives(const std::filesystem::path &root) {
  const auto package = root / "bad-source";
  auto data = manifest("demi.bad", "1.0.0");
  data.files.push_back("scripts/missing.lua");
  write(package / PackageManifestFilename,
        packageManifestJson(data).dump(2) + "\n");
  write(package / data.files.front(), "return {}\n");
  const auto archive = root / "bad.demipkg";
  Diagnostics diagnostics = createPackageArchive(package, archive);
  assert(hasErrors(diagnostics));

  const auto valid = makePackage(root, "demi.corrupt", "1.0.0");
  {
    std::fstream file(valid, std::ios::binary | std::ios::in | std::ios::out);
    file.seekp(-1, std::ios::end);
    const char byte = '\xff';
    file.write(&byte, 1);
  }
  diagnostics.clear();
  assert(!inspectPackageArchive(valid, diagnostics));
  assert(hasErrors(diagnostics));
}

} // namespace

int main() {
  const auto root = makeRoot();
  testSemanticVersions();
  testPackageNames();
  testResolverBacktracksAndRejectsCycles();
  testArchiveRegistryInstallAndFailurePreservation(root);
  testUndeclaredAndCorruptArchives(root);
  std::error_code error;
  std::filesystem::remove_all(root, error);
  return 0;
}
