#include "demi/runtime/scene/PrefabTemplateCache.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/AssetSourceFiles.h"
#include "demi/filesystem/ProjectPaths.h"
#include "demi/packages/PackageLock.h"
#include "demi/runtime/scene/composition/PrefabResolutionContext.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"

#include <algorithm>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>

namespace demi::runtime {
namespace {

using Json = nlohmann::json;

std::vector<std::filesystem::path>
assetManifestInventory(const std::filesystem::path &projectDirectory) {
  std::vector<std::filesystem::path> files;
  const auto assetsDirectory = projectDirectory / "assets";
  if (!std::filesystem::exists(assetsDirectory)) {
    return files;
  }

  for (std::filesystem::recursive_directory_iterator entry(assetsDirectory),
       end;
       entry != end; ++entry) {
    const auto name = entry->path().filename().string();
    if (entry->is_directory() && name != "generated" &&
        isInternalProjectDirectory(name)) {
      entry.disable_recursion_pending();
      continue;
    }
    if (entry->is_regular_file() && isAssetFile(entry->path())) {
      files.push_back(entry->path());
    }
  }
  std::ranges::sort(files);
  return files;
}

// Dependency discovery needs stable-ID resolution, not another validation of
// every installed package payload. Index manifest IDs without opening sources;
// parse full manifests only for resources reachable from this template.
class AssetManifestIndex {
public:
  struct Source {
    std::filesystem::path manifest;
    std::filesystem::path packageManifest;
  };

  explicit AssetManifestIndex(const std::filesystem::path &projectDirectory) {
    for (const auto &path : assetManifestInventory(projectDirectory)) {
      add(path, {});
    }
    const auto lockPath = projectDirectory / packages::PackageLockFilename;
    if (!std::filesystem::exists(lockPath)) {
      return;
    }
    const auto lock = packages::loadPackageLock(lockPath);
    if (hasErrors(lock.diagnostics)) {
      throw std::runtime_error(
          "Package lock is unavailable for cache tracking");
    }
    const bool cooked = std::filesystem::is_regular_file(projectDirectory /
                                                         "cook.manifest.json");
    for (const auto &[name, release] : lock.releases) {
      const auto root =
          projectDirectory / (cooked ? "packages" : ".demi/packages") / name;
      for (const auto &relative : release.manifest.assetManifests) {
        add(root / relative, root / "demi.package.json");
      }
    }
  }

  const Source &resolve(const std::string &id) const {
    const auto found = sources_.find(id);
    if (found == sources_.end() || found->second.size() != 1) {
      throw std::runtime_error(
          "Unresolved or ambiguous asset cache dependency: " + id);
    }
    return found->second.front();
  }

private:
  void add(const std::filesystem::path &path,
           const std::filesystem::path &packageManifest) {
    std::ifstream input(path);
    const auto document = Json::parse(input, nullptr, false);
    if (document.is_object()) {
      const auto id = document.find("id");
      if (id != document.end() && id->is_string()) {
        sources_[id->get<std::string>()].push_back({path, packageManifest});
      }
    }
  }

  std::map<std::string, std::vector<Source>> sources_;
};

// Collect dependencies through ordinary source references and imported asset
// metadata, including external glTF buffers. No special casing by prefab shape.
class DependencyCollector {
public:
  explicit DependencyCollector(std::filesystem::path projectDirectory,
                               composition::PrefabResolutionContext *resolution)
      : projectDirectory_(std::move(projectDirectory)),
        resolution_(resolution ? resolution : &ownedResolution_) {}

  void collectPrefab(const std::string &reference,
                     const std::filesystem::path &owner) {
    const auto path =
        composition::resolvePrefabReference(owner, reference, resolution_);
    if (!path) {
      throw std::runtime_error("Unresolved prefab cache dependency: " +
                               reference);
    }
    if (!prefabs_.insert(*path).second) {
      return;
    }
    files.insert(*path);
    for (const auto &directory : {projectDirectory_ / ".demi/packages",
                                  projectDirectory_ / "packages"}) {
      if (assets::pathIsInside(directory, *path)) {
        const auto relative = std::filesystem::relative(*path, directory);
        files.insert(directory / *relative.begin() / "demi.package.json");
      }
    }
    std::ifstream input(*path);
    scan(Json::parse(input), *path);
  }

  void scan(const Json &value, const std::filesystem::path &owner) {
    if (value.is_string()) {
      const auto &reference = value.get_ref<const std::string &>();
      if (reference.starts_with("prefab://")) {
        collectPrefab(reference, owner);
      } else if (reference.starts_with("asset://")) {
        collectAsset(reference);
      }
      return;
    }
    if (value.is_structured()) {
      for (const auto &child : value) {
        scan(child, owner);
      }
    }
  }

  std::set<std::filesystem::path> files;
  std::map<std::string, std::filesystem::path> assetResolutions;

private:
  void collectAsset(const std::string &id) {
    if (!assets_.insert(id).second) {
      return;
    }
    if (!manifestIndex_) {
      manifestIndex_ = std::make_unique<AssetManifestIndex>(projectDirectory_);
    }
    const auto &resolved = manifestIndex_->resolve(id);
    assetResolutions.emplace(id, resolved.manifest);
    if (!resolved.packageManifest.empty()) {
      files.insert(resolved.packageManifest);
    }
    const auto asset = loadAssetManifest(resolved.manifest);
    if (!asset || asset->id != id) {
      throw std::runtime_error(
          "Unreadable or changed asset cache dependency: " + id);
    }
    const auto sources = assets::collectAssetFiles(*asset);
    files.insert(sources.begin(), sources.end());
    for (const auto &dependency : asset->dependencies) {
      collectAsset(dependency);
    }
  }

  std::filesystem::path projectDirectory_;
  composition::PrefabResolutionContext ownedResolution_;
  composition::PrefabResolutionContext *resolution_;
  std::set<std::filesystem::path> prefabs_;
  std::set<std::string> assets_;
  std::unique_ptr<AssetManifestIndex> manifestIndex_;
};

} // namespace

void PrefabTemplateCache::configure(std::filesystem::path projectDirectory) {
  projectDirectory_ = std::move(projectDirectory);
  entries_.clear();
  hits_ = misses_ = bypasses_ = sequence_ = 0;
}

void PrefabTemplateCache::setCapacity(std::size_t entries) {
  capacity_ = entries;
  trim();
}

void PrefabTemplateCache::trim() {
  while (entries_.size() > capacity_) {
    const auto oldest = std::ranges::min_element(
        entries_, {}, [](const auto &entry) { return entry.second.lastUse; });
    entries_.erase(oldest);
  }
}

const Json *PrefabTemplateCache::find(const std::string &prefab,
                                      const Json &overrides) {
  const auto found = entries_.find({prefab, overrides.dump()});
  if (found != entries_.end()) {
    bool current = true;
    try {
      const auto &entry = found->second;
      if (!entry.dependencies.assetResolutions.empty()) {
        const AssetManifestIndex manifests(projectDirectory_);
        for (const auto &[id, path] : entry.dependencies.assetResolutions) {
          if (manifests.resolve(id).manifest != path) {
            current = false;
            break;
          }
        }
      }
      for (const auto &[path, hash] : entry.dependencies.files) {
        const auto currentHash = assets::hashFile(path);
        if (currentHash != hash ||
            (!currentHash && std::filesystem::exists(path))) {
          current = false;
          break;
        }
      }
    } catch (const std::exception &) {
      current = false;
    }
    if (current) {
      ++hits_;
      found->second.lastUse = ++sequence_;
      return &found->second.document;
    }
    entries_.erase(found);
  }
  ++misses_;
  return nullptr;
}

std::optional<PrefabTemplateDependencies> PrefabTemplateCache::dependencies(
    const std::string &prefab, const Json &overrides,
    composition::PrefabResolutionContext *resolution) const {
  if (capacity_ == 0) {
    return std::nullopt;
  }
  // Caching is optional. Missing/unreadable dependencies must never justify
  // reusing a stale document or turning a successful expansion into a failure.
  try {
    DependencyCollector dependencies(projectDirectory_, resolution);
    const auto owner = projectDirectory_ / "demi.project.json";
    dependencies.collectPrefab(prefab, owner);
    dependencies.scan(overrides, owner);
    dependencies.files.insert(projectDirectory_ / "demi.project.json");
    dependencies.files.insert(projectDirectory_ / "demi.packages.lock.json");
    dependencies.files.insert(projectDirectory_ / "cook.manifest.json");

    PrefabTemplateDependencies snapshot;
    snapshot.assetResolutions = std::move(dependencies.assetResolutions);
    for (const auto &path : dependencies.files) {
      auto hash = assets::hashFile(path);
      if (!hash && std::filesystem::exists(path)) {
        return std::nullopt;
      }
      snapshot.files.emplace(path, std::move(hash));
    }
    return snapshot;
  } catch (const std::exception &) {
    return std::nullopt;
  }
}

void PrefabTemplateCache::store(
    const std::string &prefab, const Json &overrides, const Json &document,
    const std::optional<PrefabTemplateDependencies> &beforePreparation,
    composition::PrefabResolutionContext *resolution) {
  const auto afterPreparation = dependencies(prefab, overrides, resolution);
  if (!beforePreparation || !afterPreparation ||
      *beforePreparation != *afterPreparation) {
    ++bypasses_;
    return;
  }
  Entry prepared{
      .document = document,
      .dependencies = *afterPreparation,
      .lastUse = ++sequence_};
  entries_.insert_or_assign(Key{prefab, overrides.dump()}, std::move(prepared));
  trim();
}

PrefabTemplateCacheStats PrefabTemplateCache::statistics() const {
  return {.hits = hits_, .misses = misses_, .entries = entries_.size(),
          .bypasses = bypasses_};
}

} // namespace demi::runtime
