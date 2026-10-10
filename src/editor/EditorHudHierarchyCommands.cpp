#include "demi/runtime/ui/UiStateController.h"
#include "editor/EditorHudDocument.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
namespace demi::editor {
using Json = nlohmann::json;
namespace {
bool within(std::string_view value, std::string_view prefix) {
  return value == prefix || value.starts_with(std::string(prefix) + "/");
}
void cleanRootParent(Json &node) {
  node.erase("parent");
  if (node.contains("prefab")) {
    if (auto overrides = node.find("overrides"); overrides != node.end()) {
      if (auto root = overrides->find("$root");
          root != overrides->end() && root->is_object()) {
        root->erase("parent");
        if (root->empty())
          overrides->erase(root);
      }
      if (overrides->empty())
        node.erase(overrides);
    }
  }
}
} // namespace
bool EditorHudDocument::reparentNode(std::string_view id,
                                     std::string_view parentId,
                                     std::string &error) {
  runtime::ui::UiStateController state;
  const auto *node = state.find(preview_, id),
             *parent = state.find(preview_, parentId);
  if (!node || !parent) {
    error = "The moved node or destination no longer exists.";
    return false;
  }
  if (node->parent.empty()) {
    error = "The HUD document root cannot be moved.";
    return false;
  }
  std::unordered_set<std::string> visited;
  for (auto ancestor = std::string(parentId); !ancestor.empty();) {
    if (ancestor == id || !visited.insert(ancestor).second) {
      error = "A HUD node cannot be parented beneath itself.";
      return false;
    }
    const auto *current = state.find(preview_, ancestor);
    ancestor = current ? current->parent : std::string{};
  }
  if (node->parent == parentId)
    return true;
  Json replacement = document_.json();
  std::string destinationPrefix;
  Json *destination = childStorage(replacement, parentId, destinationPrefix);
  if (!destination) {
    error = "The destination has no editable owner.";
    return false;
  }
  const auto source = composition_.authoredNodes.find(std::string(id));
  std::string destinationPath;
  if (const auto *origin = prefabOrigin(parentId)) {
    auto pointer = Json::json_pointer(origin->instancePointer);
    pointer /= "overrides";
    pointer /= origin->localNodeId.empty() ? "$root" : origin->localNodeId;
    destinationPath = pointer.to_string();
  } else
    destinationPath =
        composition_.authoredNodes.at(std::string(parentId)).pointer;
  if (source != composition_.authoredNodes.end() &&
      !within(destinationPath, source->second.pointer)) {
    Json copy = *authoredNode(id);
    cleanRootParent(copy);
    if (const auto *properties = authoringProperties(id);
        properties && properties->contains("parent")) {
      if (copy.contains("prefab"))
        copy["overrides"]["$root"]["parent"] = std::string(parentId);
      else
        copy["parent"] = std::string(parentId);
    }
    if (const auto *origin = prefabOrigin(id);
        origin && origin->instancePointer != source->second.pointer) {
      mutableNodeProperties(replacement, id)->erase("parent");
      pruneOverrides(replacement, id);
    }
    destination = childStorage(replacement, parentId, destinationPrefix);
    if (!destination->contains("children"))
      (*destination)["children"] = Json::array();
    (*destination)["children"].push_back(std::move(copy));
    auto sourcePath = Json::json_pointer(source->second.pointer);
    const auto index = std::stoull(sourcePath.back());
    sourcePath.pop_back();
    auto &siblings = replacement.at(sourcePath);
    siblings.erase(siblings.begin() +
                   static_cast<Json::difference_type>(index));
    if (siblings.empty()) {
      auto ownerPath = sourcePath;
      ownerPath.pop_back();
      auto owner = ownerPath;
      if (!owner.empty()) {
        const auto key = owner.back();
        owner.pop_back();
        if (!owner.empty() && owner.back() == "overrides") {
          auto &patch = replacement.at(ownerPath);
          patch.erase("children");
          if (patch.empty())
            replacement.at(owner).erase(key);
        }
      }
    }
  } else {
    pruneOverrides(replacement, parentId);
    Json *properties = mutableNodeProperties(replacement, id);
    if (!properties) {
      error = "The moved node has no editable owner.";
      return false;
    }
    (*properties)["parent"] = std::string(parentId);
  }
  return replaceAndRebuild(std::move(replacement), error);
}

bool EditorHudDocument::unpackPrefab(std::string_view selection,
                                     std::string &error) {
  std::string id(selection);
  const auto *instance = authoredNode(id);
  if (!instance || !instance->contains("prefab")) {
    const auto *origin = prefabOrigin(id);
    if (!origin) {
      error = "Select a prefab instance or one of its inherited nodes.";
      return false;
    }
    id = origin->instanceId;
    instance = authoredNode(id);
  }
  if (!instance || !instance->contains("prefab")) {
    error = "The owning prefab instance is unavailable.";
    return false;
  }
  const auto source = composition_.authoredNodes.at(id);
  std::unordered_set<std::string> owned;
  for (const auto &[nodeId, origin] : composition_.origins)
    if (within(origin.definitionPointer, source.pointer))
      owned.insert(nodeId);
  for (const auto &[nodeId, definition] : composition_.authoredNodes)
    if (within(definition.pointer, source.pointer))
      owned.insert(nodeId);
  owned.insert(id);
  std::unordered_map<std::string, Json> properties;
  std::unordered_map<std::string, std::vector<std::string>> children;
  std::vector<std::string> detached;
  for (const auto &node : preview_.nodes) {
    if (!owned.contains(node.id))
      continue;
    const auto *expanded = effectiveNode(node.id);
    if (!expanded)
      continue;
    Json value = Json::object();
    for (const auto &[key, item] : expanded->items())
      if (key != "children")
        value[key] = item;
    if (expanded->contains("children"))
      value["children"] = Json::array();
    value["id"] = node.id;
    value["parent"] = node.parent;
    properties.emplace(node.id, std::move(value));
    if (node.id == id)
      continue;
    if (owned.contains(node.parent))
      children[node.parent].push_back(node.id);
    else
      detached.push_back(node.id);
  }
  if (!properties.contains(id)) {
    error = "The prefab root is removed; restore it before unpacking.";
    return false;
  }
  const auto build = [&](auto &&self, const std::string &nodeId) -> Json {
    auto value = properties.at(nodeId);
    if (!children[nodeId].empty()) {
      value["children"] = Json::array();
      for (const auto &child : children[nodeId]) {
        auto nested = self(self, child);
        nested.erase("parent");
        value["children"].push_back(std::move(nested));
      }
    }
    return value;
  };
  Json unpacked = build(build, id);
  Json replacement = document_.json();
  std::unordered_set<std::string> members = owned;
  for (const auto &[nodeId, origin] : composition_.removedOrigins)
    if (within(origin.definitionPointer, source.pointer))
      members.insert(nodeId);
  for (const auto &[nodeId, definition] : composition_.removedSources)
    if (within(definition.pointer, source.pointer))
      members.insert(nodeId);
  for (const auto &nodeId : members) {
    const auto *origin = prefabOrigin(nodeId);
    if (!origin) {
      const auto removed = composition_.removedOrigins.find(nodeId);
      if (removed != composition_.removedOrigins.end())
        origin = &removed->second;
    }
    if (!origin || within(origin->instancePointer, source.pointer))
      continue;
    auto &owner = replacement.at(Json::json_pointer(origin->instancePointer));
    if (!owner.contains("overrides"))
      continue;
    auto &patches = owner["overrides"];
    const auto key =
        origin->localNodeId.empty() ? "$root" : origin->localNodeId;
    if (!patches.contains(key))
      continue;
    auto &patch = patches[key];
    if (patch.is_object() && patch.contains("children"))
      patch = Json{{"children", patch["children"]}};
    else
      patches.erase(key);
    if (patches.empty())
      owner.erase("overrides");
  }
  *mutableAuthoredNode(replacement, id) = std::move(unpacked);
  for (const auto &nodeId : detached) {
    auto value = build(build, nodeId);
    auto &root =
        replacement.contains("root") ? replacement["root"] : replacement;
    if (!root.contains("children"))
      root["children"] = Json::array();
    root["children"].push_back(std::move(value));
  }
  return replaceAndRebuild(std::move(replacement), error);
}
} // namespace demi::editor
