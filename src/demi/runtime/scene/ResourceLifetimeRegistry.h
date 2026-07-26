#pragma once

#include "demi/runtime/scene/model/World.h"

#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace demi::runtime {

class ResourceLifetimeRegistry {
public:
  void capture(std::string owner, std::span<const Entity> entities);
  void release(std::string_view owner);
  [[nodiscard]] bool owns(std::string_view owner,
                          std::string_view assetId) const;
  [[nodiscard]] std::size_t groupCount() const;

private:
  std::unordered_map<std::string, std::unordered_set<std::string>> groups_;
};

} // namespace demi::runtime
