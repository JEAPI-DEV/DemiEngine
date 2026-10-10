#include "demi/runtime/ui/UiPrefabHierarchy.h"
#include <unordered_map>
#include <vector>
namespace demi::runtime::ui {
using Json = nlohmann::json;
bool composeUiHierarchy(Json &root,
                        const std::unordered_set<std::string> &removed,
                        const std::unordered_set<std::string> &externalParents,
                        const std::filesystem::path &source,
                        Diagnostics &diagnostics) {
  struct Node {
    Json properties;
    std::string parent;
    bool hadChildren = false;
  };
  std::vector<std::string> order;
  std::unordered_map<std::string, Node> nodes;
  const auto reject = [&](const char *code, std::string message) {
    diagnostics.push_back({.severity = Severity::Error,
                           .code = code,
                           .message = std::move(message),
                           .path = source.string()});
    return false;
  };
  const auto collect = [&](auto &&self, const Json &node,
                           const std::string &parent) -> bool {
    const auto id = node.at("id").get<std::string>();
    if (nodes.contains(id))
      return reject("UI_PREFAB_ID_COLLISION", "Duplicate UI node ID: " + id);
    if (node.contains("parent") && !node["parent"].is_string())
      return reject("UI_PARENT_INVALID",
                    "UI parent must be a stable node reference.");
    if (node.contains("blocks_pointer") && !node["blocks_pointer"].is_boolean())
      return reject("UI_POINTER_BLOCK_INVALID",
                    "blocks_pointer must be a boolean.");
    Node item{Json::object(), node.value("parent", parent),
              node.contains("children")};
    for (const auto &[key, value] : node.items())
      if (key != "children")
        item.properties[key] = value;
    nodes.emplace(id, std::move(item));
    order.push_back(id);
    if (node.contains("children"))
      for (const auto &child : node["children"])
        if (!self(self, child, id))
          return false;
    return true;
  };
  if (!collect(collect, root, {}))
    return false;
  const auto rootId = root.at("id").get<std::string>();
  std::unordered_map<std::string, int> marks;
  for (const auto &id : order) {
    std::vector<std::string> path;
    auto current = id;
    while (!current.empty() && !externalParents.contains(current)) {
      const auto found = nodes.find(current);
      if (found == nodes.end())
        return reject("UI_PARENT_UNKNOWN", "Unknown UI parent: " + current);
      if (marks[current] == 2)
        break;
      if (marks[current] == 1)
        return reject("UI_PARENT_CYCLE",
                      "UI parent cycle involving: " + current);
      marks[current] = 1;
      path.push_back(current);
      current = found->second.parent;
    }
    for (const auto &item : path)
      marks[item] = 2;
  }
  if (nodes.contains(nodes.at(rootId).parent))
    return reject(
        "UI_ROOT_PARENT_INVALID",
        "The HUD document root cannot be parented inside its own document.");
  auto deleted = removed;
  for (bool changed = true; changed;) {
    changed = false;
    for (const auto &[id, node] : nodes)
      if (deleted.contains(node.parent) && deleted.insert(id).second)
        changed = true;
  }
  if (deleted.contains(rootId))
    return reject("UI_ROOT_REMOVED",
                  "The HUD root cannot be removed by an override.");
  std::unordered_map<std::string, std::vector<std::string>> children;
  std::vector<std::string> detached;
  for (const auto &id : order) {
    if (id == rootId || deleted.contains(id))
      continue;
    const auto &parent = nodes.at(id).parent;
    if (parent.empty() || externalParents.contains(parent))
      detached.push_back(id);
    else
      children[parent].push_back(id);
  }
  const auto build = [&](auto &&self, const std::string &id) -> Json {
    auto result = nodes.at(id).properties;
    result["parent"] = nodes.at(id).parent;
    if (!children[id].empty()) {
      result["children"] = Json::array();
      for (const auto &child : children[id])
        result["children"].push_back(self(self, child));
    } else if (nodes.at(id).hadChildren) {
      // Preserve the omitted-size fill convention for empty layout containers.
      result["children"] = Json::array();
    }
    return result;
  };
  root = build(build, rootId);
  for (const auto &id : detached) {
    if (!root.contains("children"))
      root["children"] = Json::array();
    root["children"].push_back(build(build, id));
  }
  return true;
}
} // namespace demi::runtime::ui
