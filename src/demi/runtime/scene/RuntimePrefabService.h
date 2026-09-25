#pragma once

#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/scene/WorldCommandBuffer.h"
#include "demi/runtime/scene/PrefabTemplateCache.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

struct PrefabInstantiateOptions {
  std::string id;
  std::optional<Vec3> position;
  nlohmann::json overrides = nlohmann::json::object();
  bool pooled = false;
};

struct PrefabInstanceResult {
  std::string instanceId;
  std::vector<std::string> entityIds;
  Diagnostics diagnostics;

  [[nodiscard]] explicit operator bool() const {
    return !instanceId.empty() && !hasErrors(diagnostics);
  }
};

class RuntimePrefabService {
public:
  void configure(std::filesystem::path projectDirectory);
  void setTemplateCacheCapacity(std::size_t entries) { templates_.setCapacity(entries); }
  PrefabTemplateCacheStats templateCacheStatistics() const { return templates_.statistics(); }

  [[nodiscard]] PrefabInstanceResult instantiate(
      World &world, WorldCommandBuffer &commands, std::string prefab,
      PrefabInstantiateOptions options);
  [[nodiscard]] bool release(World &world, WorldCommandBuffer &commands,
                             const std::string &instanceId);
  void prune(const World &world);
  [[nodiscard]] std::size_t pooledCount(std::string_view prefab) const;

private:
  struct Instance {
    std::string prefab;
    std::vector<std::string> entityIds;
    bool pooled = false;
    bool available = false;
  };

  [[nodiscard]] PrefabInstanceResult build(
      std::string_view prefab, const PrefabInstantiateOptions &options,
      nlohmann::json &expanded);

  std::filesystem::path projectDirectory_;
  std::unordered_map<std::string, Instance> instances_;
  PrefabTemplateCache templates_;
};

} // namespace demi::runtime
