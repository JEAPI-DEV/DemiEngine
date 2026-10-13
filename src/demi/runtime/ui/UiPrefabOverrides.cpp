#include "demi/runtime/ui/UiPrefabOverrides.h"
#include <algorithm>
#include <unordered_set>

namespace demi::runtime::ui {
namespace {
using Json = nlohmann::json;
Json *find(Json &node, const std::string &id) {
  if (node.value("id", std::string{}) == id)
    return &node;
  if (node.contains("children"))
    for (auto &child : node["children"])
      if (auto *found = find(child, id))
        return found;
  return nullptr;
}
void patchProperties(Json &node, const Json &patch) {
  if (patch.contains("dock") && !patch["dock"].is_null()) {
    node.erase("anchor_min");
    node.erase("anchor_max");
    node.erase("position");
    node.erase("at");
  }
  if (patch.contains("anchor_min") || patch.contains("anchor_max"))
    node.erase("dock");
  for (const auto &[shortName, canonical] :
       {std::pair{"at", "position"}, {"pad", "padding"}, {"stack", "layout"}}) {
    if (patch.contains(shortName))
      node.erase(canonical);
    if (patch.contains(canonical))
      node.erase(shortName);
  }
  for (const auto &[field, value] : patch.items()) {
    if (field == "children")
      continue;
    if (value.is_null())
      node.erase(field);
    else
      node[field] = value;
  }
}
} // namespace
bool applyUiPrefabOverrides(
    Json &root, const Json &instance, const std::filesystem::path &source,
    Diagnostics &diagnostics, const UiOverrideChildExpander &expandChild,
    std::unordered_set<std::string> &removed,
    const std::unordered_map<std::string, std::string> &inheritedIds) {
  const auto reject = [&](std::string code, std::string message) {
    diagnostics.push_back(
        {.severity = Severity::Error,
         .code = std::move(code),
         .message = std::move(message),
         .path = source.string(),
         .suggestion =
             "Use overrides keyed by $root or a stable prefab-local node ID."});
    return false;
  };
  if (instance.contains("node_overrides"))
    return reject("UI_PREFAB_OVERRIDES_INVALID",
                  "Move descendant patches into the single overrides map and "
                  "root fields under overrides.$root.");
  std::unordered_set<std::string> ids;
  const auto unique = [&](auto &&self, const Json &node, bool isRoot) -> bool {
    const auto id = node.at("id").get<std::string>();
    if ((!isRoot && id == "$root") || !ids.insert(id).second)
      return false;
    if (node.contains("children"))
      for (const auto &child : node["children"])
        if (!self(self, child, false))
          return false;
    return true;
  };
  if (!unique(unique, root, true))
    return reject("UI_PREFAB_ID_COLLISION",
                  "Local additions must keep prefab-local IDs unique; $root is "
                  "reserved.");
  if (!instance.contains("overrides"))
    return true;
  const auto &patches = instance["overrides"];
  if (!patches.is_object())
    return reject("UI_PREFAB_OVERRIDES_INVALID",
                  "UI prefab overrides must be a target map.");
  for (const auto &[id, patch] : patches.items()) {
    const auto mapped = inheritedIds.find(id);
    Json *target = id == "$root"                  ? &root
                   : mapped == inheritedIds.end() ? nullptr
                                                  : find(root, mapped->second);
    if (!target || (id != "$root" && target == &root))
      return reject("UI_PREFAB_OVERRIDE_NODE_MISSING",
                    "Unknown inherited target: " + id);
    if (patch.is_null()) {
      if (id == "$root")
        return reject("UI_PREFAB_OVERRIDE_RESERVED",
                      "Remove the instance itself instead of removing $root.");
      removed.insert(target->at("id").get<std::string>());
      continue;
    }
    if (!patch.is_object())
      return reject("UI_PREFAB_OVERRIDES_INVALID",
                    "Targets need property objects or null; root fields belong "
                    "under $root.");
    for (const auto &[field, value] : patch.items()) {
      if (field == "id" || field == "prefab" || field == "arguments" ||
          field == "overrides" || field == "node_overrides")
        return reject(
            "UI_PREFAB_OVERRIDE_RESERVED",
            "Override cannot replace identity or composition field: " + field);
      if (field == "children" && !value.is_array())
        return reject("UI_PREFAB_OVERRIDES_INVALID",
                      "Local children must be an array.");
    }
    removed.erase(target->at("id").get<std::string>());
    patchProperties(*target, patch);
    if (patch.contains("children")) {
      if (!target->contains("children"))
        (*target)["children"] = Json::array();
      for (std::size_t i = 0; i < patch["children"].size(); ++i) {
        const auto child = expandChild(patch["children"][i], id, i);
        if (!child)
          return false;
        (*target)["children"].push_back(*child);
      }
    }
  }
  ids.clear();
  if (!unique(unique, root, true))
    return reject("UI_PREFAB_ID_COLLISION",
                  "Local additions must keep node IDs unique.");
  return true;
}
} // namespace demi::runtime::ui
