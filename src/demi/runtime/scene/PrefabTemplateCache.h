#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace demi::runtime {

struct PrefabTemplateCacheStats {
  std::size_t hits = 0;
  std::size_t misses = 0;
  std::size_t entries = 0;
  std::size_t bypasses = 0;
};

struct PrefabTemplateDependencies {
  std::map<std::filesystem::path, std::optional<std::string>> files;
  std::vector<std::filesystem::path> assetManifests;
  bool usesAssets = false;
  bool operator==(const PrefabTemplateDependencies &) const = default;
};

// Stores prepared, unowned documents. Live instances never share mutable state.
class PrefabTemplateCache {
public:
  void configure(std::filesystem::path projectDirectory);
  void setCapacity(std::size_t entries);
  const nlohmann::json *find(const std::string &prefab,
                             const nlohmann::json &overrides);
  std::optional<PrefabTemplateDependencies>
  dependencies(const std::string &prefab,
               const nlohmann::json &overrides) const;
  void
  store(const std::string &prefab, const nlohmann::json &overrides,
        const nlohmann::json &document,
        const std::optional<PrefabTemplateDependencies> &beforePreparation);
  PrefabTemplateCacheStats statistics() const;

private:
  using Key = std::pair<std::string, std::string>;
  struct Entry {
    nlohmann::json document;
    PrefabTemplateDependencies dependencies;
    std::uint64_t lastUse = 0;
  };

  void trim();
  std::filesystem::path projectDirectory_;
  std::map<Key, Entry> entries_;
  std::size_t capacity_ = 16;
  std::size_t hits_ = 0;
  std::size_t misses_ = 0;
  std::size_t bypasses_ = 0;
  std::uint64_t sequence_ = 0;
};

} // namespace demi::runtime
