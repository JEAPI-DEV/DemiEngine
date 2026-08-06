#pragma once

#include "demi/runtime/scene/model/World.h"

#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime {

class ResourceLifetimeRegistry {
public:
  using Acquire = std::function<bool(std::string_view, std::string &)>;
  using Release = std::function<void(std::string_view)>;

  ResourceLifetimeRegistry() = default;
  ResourceLifetimeRegistry(Acquire acquire, Release release);

  void capture(std::string owner, std::span<const Entity> entities);
  [[nodiscard]] bool tryCapture(std::string owner,
                                std::span<const Entity> entities,
                                std::string &error);
  void release(std::string_view owner);
  void clear();
  [[nodiscard]] bool owns(std::string_view owner,
                          std::string_view assetId) const;
  [[nodiscard]] bool isReferenced(std::string_view assetId) const;
  [[nodiscard]] std::size_t referenceCount(std::string_view assetId) const;
  [[nodiscard]] std::size_t groupCount() const;

private:
  [[nodiscard]] std::unordered_set<std::string>
  collect(std::string_view owner, std::span<const Entity> entities) const;
  [[nodiscard]] std::size_t referenceCountExcluding(
      std::string_view assetId, std::string_view excludedOwner) const;

  std::unordered_map<std::string, std::unordered_set<std::string>> groups_;
  Acquire acquire_;
  Release release_;
};

} // namespace demi::runtime
