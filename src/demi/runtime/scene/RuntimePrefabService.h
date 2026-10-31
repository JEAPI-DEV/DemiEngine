#pragma once

#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/scene/PrefabTemplateCache.h"
#include "demi/runtime/scene/WorldCommandBuffer.h"
#include "demi/runtime/scene/model/SceneTypes.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi {
struct AssetRegistry;
}

namespace demi::runtime {
namespace composition {
class PrefabResolutionContext;
}

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

// Immutable, unpublished instance snapshot. Preparing it creates no live
// ownership, Lua lifecycle calls or physics bodies. Adoption rechecks live IDs.
class PreparedPrefabInstance {
public:
  [[nodiscard]] explicit operator bool() const { return bool(result_); }
  [[nodiscard]] const PrefabInstanceResult &result() const { return result_; }
  [[nodiscard]] const std::string &prefab() const { return prefab_; }
  [[nodiscard]] std::span<const Entity> entities() const { return entities_; }

private:
  friend class RuntimePrefabService;
  PrefabInstanceResult result_;
  std::string prefab_;
  std::filesystem::path projectRoot_;
  std::vector<Entity> entities_;
  bool pooled_ = false;
};

class RuntimePrefabService {
public:
  class PreparationBatch {
  public:
    PreparationBatch(const PreparationBatch &) = delete;
    PreparationBatch &operator=(const PreparationBatch &) = delete;
    PreparationBatch(PreparationBatch &&other) noexcept;
    ~PreparationBatch();
    [[nodiscard]] std::size_t packageVerificationCount() const noexcept;
    [[nodiscard]] composition::PrefabResolutionContext &
    resolutionContext() const noexcept;

  private:
    friend class RuntimePrefabService;
    PreparationBatch(
        RuntimePrefabService &owner,
        std::shared_ptr<composition::PrefabResolutionContext> previous);
    RuntimePrefabService *owner_;
    std::shared_ptr<composition::PrefabResolutionContext> previous_;
    std::shared_ptr<composition::PrefabResolutionContext> context_;
  };

  // Nested operations share the caller's verified source inventory. The
  // outermost scope releases it; later batches validate packages afresh.
  [[nodiscard]] PreparationBatch
  beginPreparationBatch(const AssetRegistry *verifiedRegistry = nullptr);
  void configure(std::filesystem::path projectDirectory);
  void setTemplateCacheCapacity(std::size_t entries) {
    templates_.setCapacity(entries);
  }
  PrefabTemplateCacheStats templateCacheStatistics() const {
    return templates_.statistics();
  }

  // Expands and parses an unowned template without allocating a live instance.
  // Render-only consumers can inspect shared geometry before deciding whether
  // gameplay entities are needed.
  [[nodiscard]] std::optional<std::vector<Entity>>
  prepareEntities(std::string_view prefab, Diagnostics &diagnostics);

  // Run preparation on a thread-confined service. A snapshot can be adopted by
  // another service configured for the same project. The caller owns stale
  // request cancellation; source files are not reread during adoption.
  [[nodiscard]] PreparedPrefabInstance
  prepareInstance(std::string prefab, const PrefabInstantiateOptions &options);
  // Main-thread operation. Validation failures leave commands and ownership
  // unchanged. Callbacks/physics retain the ordinary scene lifecycle.
  [[nodiscard]] PrefabInstanceResult
  adoptPrepared(World &world, WorldCommandBuffer &commands,
                const PreparedPrefabInstance &prepared);

  [[nodiscard]] PrefabInstanceResult
  instantiate(World &world, WorldCommandBuffer &commands, std::string prefab,
              PrefabInstantiateOptions options);
  [[nodiscard]] std::optional<std::string_view>
  sourcePrefab(std::string_view instanceId) const;
  // Atomically prepares a replacement under the same durable instance ID.
  // Failed preparation leaves live ownership and pending commands unchanged.
  [[nodiscard]] PrefabInstanceResult replace(World &world,
                                             WorldCommandBuffer &commands,
                                             std::string prefab,
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

  [[nodiscard]] PrefabInstanceResult
  build(std::string_view prefab, const PrefabInstantiateOptions &options,
        nlohmann::json &expanded);

  std::filesystem::path projectDirectory_;
  std::filesystem::path projectRoot_;
  std::unordered_map<std::string, Instance> instances_;
  PrefabTemplateCache templates_;
  std::shared_ptr<composition::PrefabResolutionContext> resolution_;
};

} // namespace demi::runtime
