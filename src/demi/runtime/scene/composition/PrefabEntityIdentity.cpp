#include "demi/runtime/scene/composition/PrefabEntityIdentity.h"
#include <set>
#include <stdexcept>

namespace demi::runtime::composition {
PrefabEntityIdMap prefabEntityIdRemapping(
    const nlohmann::json &mapping, const std::vector<std::string> &expandedIds,
    std::string_view instancePrefix, std::string_view ownerPrefix) {
  if (!mapping.is_object())
    throw std::invalid_argument(
        "entity_ids must be an object of local IDs to owner-document IDs");
  const std::set<std::string> known(expandedIds.begin(), expandedIds.end());
  const std::string prefix = std::string(instancePrefix) + "/";
  for (const auto &[local, target] : mapping.items()) {
    if (local.empty() || !target.is_string() ||
        target.get_ref<const std::string &>().empty())
      throw std::invalid_argument(
          "entity_ids requires nonempty local and destination IDs");
    if (!known.contains(prefix + local))
      throw std::invalid_argument("entity_ids names a missing prefab entity: " +
                                  local);
  }
  PrefabEntityIdMap result;
  std::set<std::string> destinations;
  for (const auto &id : expandedIds) {
    std::string destination = id;
    if (id.starts_with(prefix)) {
      auto local = id.substr(prefix.size());
      for (;;) {
        if (const auto target = mapping.find(local); target != mapping.end()) {
          destination =
              (ownerPrefix.empty() ? "" : std::string(ownerPrefix) + "/") +
              target->get<std::string>() +
              id.substr(prefix.size() + local.size());
          break;
        }
        const auto slash = local.rfind('/');
        if (slash == std::string::npos)
          break;
        local.resize(slash);
      }
    }
    if (!destinations.insert(destination).second)
      throw std::invalid_argument(
          "entity_ids produces a duplicate entity ID: " + destination);
    result.emplace(id, std::move(destination));
  }
  return result;
}
} // namespace demi::runtime::composition
