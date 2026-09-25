#include "demi/runtime/scene/PrefabTemplateCache.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/AssetRegistry.h"
#include "demi/assets/AssetSourceFiles.h"
#include "demi/filesystem/ProjectPaths.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"

#include <algorithm>
#include <fstream>
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

// Collect dependencies through ordinary source references and imported asset
// metadata, including external glTF buffers. No special casing by prefab shape.
class DependencyCollector {
public:
  explicit DependencyCollector(std::filesystem::path projectDirectory)
      : projectDirectory_(std::move(projectDirectory)) {}

  void collectPrefab(const std::string &reference) {
    const auto path = composition::resolvePrefabReference(
        projectDirectory_ / "demi.project.json", reference);
    if (!path) {
      throw std::runtime_error("Unresolved prefab cache dependency: " +
                               reference);
    }
    if (!prefabs_.insert(*path).second) {
      return;
    }
    files.insert(*path);
    std::ifstream input(*path);
    scan(Json::parse(input));
  }

  void scan(const Json &value) {
    if (value.is_string()) {
      const auto &reference = value.get_ref<const std::string &>();
      if (reference.starts_with("prefab://")) {
        collectPrefab(reference);
      } else if (reference.starts_with("asset://")) {
        collectAsset(reference);
      }
      return;
    }
    if (value.is_structured()) {
      for (const auto &child : value) {
        scan(child);
      }
    }
  }

  std::set<std::filesystem::path> files;
  bool usesAssets = false;

private:
  void collectAsset(const std::string &id) {
    if (!assets_.insert(id).second) {
      return;
    }
    usesAssets = true;
    if (!registry_) {
      registry_ = loadAssetRegistry(projectDirectory_);
      if (hasErrors(registry_->diagnostics)) {
        throw std::runtime_error(
            "Asset registry is unavailable for cache tracking");
      }
      // Asset IDs are resolved by the registry, so manifest edits can change
      // resolution even when the previous source asset itself did not change.
      for (const auto &registered : registry_->assets) {
        files.insert(registered.manifestPath);
        if (!registered.sourcePackage.empty()) {
          files.insert(projectDirectory_ / ".demi/packages" /
                       registered.sourcePackage / "demi.package.json");
          files.insert(projectDirectory_ / "packages" /
                       registered.sourcePackage / "demi.package.json");
        }
      }
    }
    const auto *asset = findAsset(*registry_, id);
    if (!asset) {
      throw std::runtime_error("Unresolved asset cache dependency: " + id);
    }
    const auto sources = assets::collectAssetFiles(*asset);
    files.insert(sources.begin(), sources.end());
    const auto primarySources =
        assets::collectReferencedSourceFiles(asset->sourcePath);
    files.insert(primarySources.begin(), primarySources.end());
    for (const auto &dependency : asset->dependencies) {
      collectAsset(dependency);
    }
  }

  std::filesystem::path projectDirectory_;
  std::set<std::filesystem::path> prefabs_;
  std::set<std::string> assets_;
  std::optional<AssetRegistry> registry_;
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
      if (entry.dependencies.usesAssets &&
          entry.dependencies.assetManifests !=
              assetManifestInventory(projectDirectory_)) {
        current = false;
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

std::optional<PrefabTemplateDependencies>
PrefabTemplateCache::dependencies(const std::string &prefab,
                                  const Json &overrides) const {
  if (capacity_ == 0) {
    return std::nullopt;
  }
  // Caching is optional. Missing/unreadable dependencies must never justify
  // reusing a stale document or turning a successful expansion into a failure.
  try {
    DependencyCollector dependencies(projectDirectory_);
    dependencies.collectPrefab(prefab);
    dependencies.scan(overrides);
    dependencies.files.insert(projectDirectory_ / "demi.project.json");
    dependencies.files.insert(projectDirectory_ / "demi.packages.lock.json");
    dependencies.files.insert(projectDirectory_ / "cook.manifest.json");

    PrefabTemplateDependencies snapshot;
    snapshot.usesAssets = dependencies.usesAssets;
    if (snapshot.usesAssets) {
      snapshot.assetManifests = assetManifestInventory(projectDirectory_);
    }
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
    const std::optional<PrefabTemplateDependencies> &beforePreparation) {
  const auto afterPreparation = dependencies(prefab, overrides);
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
