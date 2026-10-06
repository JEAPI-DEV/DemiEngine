#include "demi/assets/TerrainAssetStorage.h"

#include "demi/assets/AssetHash.h"
#include "demi/assets/TerrainAsset.h"
#include "demi/assets/TerrainAssetPayload.h"
#include "demi/runtime/terrain/TerrainCook.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

namespace demi::assets::terrain_storage {
namespace {

std::string hashText(std::string_view value) {
  return hashBytes(std::span(
      reinterpret_cast<const unsigned char *>(value.data()), value.size()));
}

std::filesystem::path desktopCacheRoot() {
#if defined(__ANDROID__)
  throw std::runtime_error(
      "Android terrain assets must use shipped .terrain.bin payloads");
#else
  if (const char *root = std::getenv("XDG_CACHE_HOME"); root && *root) {
    const std::filesystem::path path(root);
    if (!path.is_absolute())
      throw std::runtime_error("XDG_CACHE_HOME must be an absolute path");
    return path;
  }
  if (const char *home = std::getenv("HOME"); home && *home) {
    const std::filesystem::path path(home);
    if (!path.is_absolute())
      throw std::runtime_error(
          "HOME must be an absolute path for terrain preparation");
    return path / ".cache";
  }
  throw std::runtime_error("Terrain preparation needs XDG_CACHE_HOME or HOME "
                           "to locate the user cache");
#endif
}

std::vector<std::byte> readBytes(const std::filesystem::path &path) {
  std::error_code error;
  const auto fileSize = std::filesystem::file_size(path, error);
  if (error)
    throw std::runtime_error("Cannot read prepared terrain " + path.string() +
                             ": " + error.message());
  std::vector<std::byte> data;
  if (fileSize > data.max_size())
    throw std::length_error("Prepared terrain exceeds addressable memory");
  data.resize(static_cast<std::size_t>(fileSize));
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("Cannot open prepared terrain " + path.string());
  constexpr std::size_t Block = 64 * 1024;
  for (std::size_t offset = 0; offset < data.size();) {
    const auto count = std::min(Block, data.size() - offset);
    input.read(reinterpret_cast<char *>(data.data() + offset),
               static_cast<std::streamsize>(count));
    if (input.gcount() != static_cast<std::streamsize>(count))
      throw std::runtime_error(
          "Prepared terrain was truncated while reading: " + path.string());
    offset += count;
  }
  if (input.peek() != std::char_traits<char>::eof())
    throw std::runtime_error("Prepared terrain changed while reading: " +
                             path.string());
  return data;
}

struct MemoryEntry {
  std::weak_ptr<const runtime::HeightField> field;
  std::string recipeDigest;
  std::string inputFingerprint;
  std::uintmax_t fileSize = 0;
  std::filesystem::file_time_type modified;
};

struct MemoryCache {
  std::mutex mutex;
  std::map<std::string, MemoryEntry> entries;
};

MemoryCache &memoryCache() {
  static MemoryCache cache;
  return cache;
}

struct FileRevision {
  std::uintmax_t size = 0;
  std::filesystem::file_time_type modified;
  bool operator==(const FileRevision &) const = default;
};

FileRevision fileRevision(const std::filesystem::path &path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error)
    throw std::runtime_error("Cannot stat prepared terrain " + path.string() +
                             ": " + error.message());
  const auto modified = std::filesystem::last_write_time(path, error);
  if (error)
    throw std::runtime_error("Cannot stat prepared terrain " + path.string() +
                             ": " + error.message());
  return {size, modified};
}

std::string cachePrefix(const std::filesystem::path &path) {
  const auto absolute = std::filesystem::absolute(path).lexically_normal();
  return absolute.generic_string() + "\n";
}

} // namespace

std::filesystem::path cachePath(const AssetRegistry &registry,
                                std::string_view assetId,
                                std::string_view recipeDigest,
                                std::string_view inputFingerprint) {
  if (registry.projectDirectory.empty())
    throw std::invalid_argument(
        "Terrain asset registry has no project directory");
  const auto project =
      std::filesystem::absolute(registry.projectDirectory).lexically_normal();
  const auto projectKey = hashText(project.generic_string());
  const auto assetKey = hashText(assetId);
  const auto contentKey = hashText(
      std::to_string(terrainAssetPayloadVersion) + "\n" +
      std::to_string(runtime::terrainCookVersion) + "\n" +
      std::to_string(runtime::terrainGeneratorVersion) + "\n" +
      std::string(recipeDigest) + "\n" + std::string(inputFingerprint));
  return desktopCacheRoot() / "demiengine" / "terrain" / projectKey / assetKey /
         (contentKey + ".terrain.bin");
}

std::string assetInputFingerprint(
    const AssetRegistry &registry, const runtime::TerrainRecipe &recipe,
    const runtime::TerrainGenerationInputs &generationInputs) {
  nlohmann::json inputs{{"palette", generationInputs.fingerprint},
                        {"assets", nlohmann::json::object()}};
  std::set<std::string> references;
  auto generationRecipe = recipe.toJson();
  for (auto &biome : generationRecipe["biomes"])
    biome.erase("material");
  // Appearance assets are loaded by the renderer, not consumed by terrain
  // generation. Their references remain in the recipe digest and cook closure.
  for (const auto &id : extractAssetReferences(generationRecipe.dump()))
    references.insert(id);
  if (generationInputs.palette) {
    for (const auto &id : generationInputs.palette->assetDependencies())
      if (id.starts_with("asset://"))
        references.insert(id);
  }
  for (const auto &id : references) {
    const auto *manifest = findAsset(registry, id);
    if (!manifest)
      throw std::invalid_argument("Terrain references missing asset " + id);
    if (manifest->sourceHash.empty() && manifest->packageContentHash.empty())
      throw std::invalid_argument("Terrain input " + id +
                                  " has no verified source hash; reimport or "
                                  "refresh the asset registry");
    const auto settings = manifest->settingsJson.empty()
                              ? nlohmann::json::object()
                              : nlohmann::json::parse(manifest->settingsJson);
    inputs["assets"][id] = {{"source_hash", manifest->sourceHash},
                            {"package_hash", manifest->packageContentHash},
                            {"importer", manifest->importer},
                            {"importer_version", manifest->importerVersion},
                            {"settings", settings}};
  }
  return hashText(inputs.dump());
}

PreparedField read(const std::filesystem::path &path) {
  const auto revision = fileRevision(path);
  const auto prefix = cachePrefix(path);
  auto &cache = memoryCache();
  {
    std::scoped_lock lock(cache.mutex);
    for (auto found = cache.entries.lower_bound(prefix);
         found != cache.entries.end() && found->first.starts_with(prefix);
         ++found) {
      const auto &entry = found->second;
      if (entry.fileSize == revision.size &&
          entry.modified == revision.modified) {
        if (auto live = entry.field.lock())
          return {std::move(live), entry.recipeDigest, entry.inputFingerprint};
      }
    }
  }
  const auto data = readBytes(path);
  if (fileRevision(path) != revision)
    throw std::runtime_error("Prepared terrain changed while loading: " +
                             path.string());
  const std::string identity =
      prefix +
      hashBytes(std::span(reinterpret_cast<const unsigned char *>(data.data()),
                          data.size()));
  {
    std::scoped_lock lock(cache.mutex);
    if (const auto found = cache.entries.find(identity);
        found != cache.entries.end()) {
      if (auto live = found->second.field.lock())
        return {std::move(live), found->second.recipeDigest,
                found->second.inputFingerprint};
    }
  }
  PreparedField loaded;
  loaded.field = terrain_payload::decode(data, &loaded.recipeDigest,
                                         &loaded.inputFingerprint);
  {
    std::scoped_lock lock(cache.mutex);
    auto &entry = cache.entries[identity];
    if (auto live = entry.field.lock())
      return {std::move(live), entry.recipeDigest, entry.inputFingerprint};
    entry = {loaded.field, loaded.recipeDigest, loaded.inputFingerprint,
             revision.size, revision.modified};
    if (cache.entries.size() % 64 == 0)
      std::erase_if(cache.entries, [](const auto &item) {
        return item.second.field.expired();
      });
  }
  return loaded;
}

void writeAtomically(const std::filesystem::path &path,
                     std::span<const std::byte> bytes) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error)
    throw std::runtime_error("Cannot create terrain cache directory: " +
                             error.message());
  static std::atomic<std::uint64_t> sequence{0};
  const auto nonce =
      std::chrono::steady_clock::now().time_since_epoch().count();
  const auto temporary = std::filesystem::path(
      path.string() + "." + std::to_string(nonce) + "." +
      std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)) +
      ".tmp");
  bool temporaryCreated = false;
  try {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output)
      throw std::runtime_error("Cannot create temporary terrain cache " +
                               temporary.string());
    temporaryCreated = true;
    constexpr std::size_t Block = 64 * 1024;
    for (std::size_t offset = 0; offset < bytes.size();) {
      const auto count = std::min(Block, bytes.size() - offset);
      output.write(reinterpret_cast<const char *>(bytes.data() + offset),
                   static_cast<std::streamsize>(count));
      if (!output)
        throw std::runtime_error("Could not write complete terrain cache");
      offset += count;
    }
    output.flush();
    if (!output)
      throw std::runtime_error("Could not flush terrain cache");
    output.close();
    if (!output)
      throw std::runtime_error("Could not close terrain cache");
    std::filesystem::rename(temporary, path, error);
    if (error)
      throw std::runtime_error("Could not publish terrain cache: " +
                               error.message());
  } catch (...) {
    if (temporaryCreated) {
      std::error_code ignored;
      std::filesystem::remove(temporary, ignored);
    }
    throw;
  }
}

} // namespace demi::assets::terrain_storage
