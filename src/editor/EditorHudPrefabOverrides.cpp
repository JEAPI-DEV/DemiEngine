#include "editor/EditorHudDocument.h"

namespace demi::editor {
using Json = nlohmann::json;
const Json *EditorHudDocument::authoredNode(std::string_view id) const {
  const auto found = composition_.authoredNodes.find(std::string(id));
  if (found == composition_.authoredNodes.end())
    return nullptr;
  const auto pointer = Json::json_pointer(found->second.pointer);
  return document_.json().contains(pointer) ? &document_.json().at(pointer)
                                            : nullptr;
}
Json *EditorHudDocument::mutableAuthoredNode(Json &document,
                                             std::string_view id) const {
  const auto found = composition_.authoredNodes.find(std::string(id));
  if (found == composition_.authoredNodes.end())
    return nullptr;
  const auto pointer = Json::json_pointer(found->second.pointer);
  return document.contains(pointer) ? &document.at(pointer) : nullptr;
}

const runtime::ui::UiPrefabNodeOrigin *
EditorHudDocument::prefabOrigin(std::string_view id) const {
  const auto found = composition_.origins.find(std::string(id));
  return found == composition_.origins.end() ? nullptr : &found->second;
}

const Json *EditorHudDocument::authoringProperties(std::string_view id) const {
  const auto found = composition_.authoringProperties.find(std::string(id));
  return found == composition_.authoringProperties.end() ? effectiveNode(id)
                                                         : &found->second;
}

const Json *EditorHudDocument::nodeOverrides(std::string_view id) const {
  const auto *origin = prefabOrigin(id);
  if (!origin)
    return nullptr;
  const auto &instance =
      document_.json().at(Json::json_pointer(origin->instancePointer));
  const auto patches = instance.find("overrides");
  if (patches == instance.end())
    return nullptr;
  const auto found = patches->find(
      origin->localNodeId.empty() ? "$root" : origin->localNodeId);
  return found == patches->end() ? nullptr : &*found;
}

std::optional<Json>
EditorHudDocument::prefabArguments(std::string_view id,
                                   std::string &error) const {
  const auto found = composition_.instanceArguments.find(std::string(id));
  if (found == composition_.instanceArguments.end()) {
    error = "Select an authored prefab instance to edit its parameters.";
    return std::nullopt;
  }
  return found->second;
}

Json *EditorHudDocument::mutableNodeProperties(Json &document,
                                               std::string_view id) {
  const auto *origin = prefabOrigin(id);
  if (!origin)
    return mutableAuthoredNode(document, id);
  auto &instance = document.at(Json::json_pointer(origin->instancePointer));
  auto &patch =
      instance["overrides"]
              [origin->localNodeId.empty() ? "$root" : origin->localNodeId];
  if (patch.is_null())
    patch = Json::object();
  return &patch;
}
void EditorHudDocument::discardOverridesForSource(Json &document,
                                                  std::string_view pointer,
                                                  bool deleting) const {
  const auto within = [&](const std::string &value) {
    return value == pointer || value.starts_with(std::string(pointer) + "/");
  };
  std::unordered_set<std::string> removed;
  for (const auto &[id, source] : composition_.authoredNodes)
    if (within(source.pointer))
      removed.insert(id);
  for (const auto &[id, origin] : composition_.origins)
    if (within(origin.definitionPointer))
      removed.insert(id);
  for (bool changed = true; changed;) {
    changed = false;
    for (const auto &node : preview_.nodes)
      if (removed.contains(node.parent) && removed.insert(node.id).second)
        changed = true;
  }
  for (const auto &id : removed)
    if (const auto *origin = prefabOrigin(id);
        origin && !removed.contains(origin->instanceId)) {
      auto &instance = document.at(Json::json_pointer(origin->instancePointer));
      const auto target =
          origin->localNodeId.empty() ? "$root" : origin->localNodeId;
      if (deleting && !within(origin->definitionPointer))
        instance["overrides"][target] = nullptr;
      else if (auto overrides = instance.find("overrides");
               overrides != instance.end())
        overrides->erase(target);
    }
}

void EditorHudDocument::pruneOverrides(Json &document,
                                       std::string_view id) const {
  const auto *origin = prefabOrigin(id);
  if (!origin)
    return;
  auto &instance = document.at(Json::json_pointer(origin->instancePointer));
  if (auto patches = instance.find("overrides"); patches != instance.end()) {
    const auto patch = patches->find(
        origin->localNodeId.empty() ? "$root" : origin->localNodeId);
    if (patch != patches->end() && patch->is_object() && patch->empty())
      patches->erase(patch);
    if (patches->empty())
      instance.erase(patches);
  }
}
Json *EditorHudDocument::childStorage(Json &document, std::string_view parentId,
                                      std::string &prefix) {
  const std::string parent =
      parentId.empty()
          ? (document.contains("root")
                 ? document["root"].value("id", std::string("ui_root"))
                 : "ui_root")
          : std::string(parentId);
  if (const auto *origin = prefabOrigin(parent)) {
    prefix.clear();
    return mutableNodeProperties(document, parent);
  }
  const auto found = composition_.authoredNodes.find(parent);
  if (found == composition_.authoredNodes.end())
    return nullptr;
  prefix.clear();
  return mutableAuthoredNode(document, parent);
}
std::string EditorHudDocument::newChildId(std::string_view base,
                                          std::string_view prefix) const {
  std::string id(base);
  for (unsigned suffix = 2;
       composition_.reservedIds.contains(std::string(prefix) + id); ++suffix)
    id = std::string(base) + "_" + std::to_string(suffix);
  return id;
}

bool EditorHudDocument::resetNodeOverride(std::string_view id,
                                          std::string_view field,
                                          std::string &error) {
  if (!prefabOrigin(id)) {
    error = "The selected HUD node is not a prefab instance node.";
    return false;
  }
  Json replacement = document_.json();
  if (field.empty() || field == "children") {
    const auto *origin = prefabOrigin(id);
    auto pointer = Json::json_pointer(origin->instancePointer);
    pointer /= "overrides";
    pointer /= origin->localNodeId.empty() ? "$root" : origin->localNodeId;
    pointer /= "children";
    discardOverridesForSource(replacement, pointer.to_string());
  }
  Json *patch = mutableNodeProperties(replacement, id);
  if (!patch)
    return false;
  if (field.empty())
    patch->clear();
  else
    patch->erase(std::string(field));
  pruneOverrides(replacement, id);
  return replaceAndRebuild(std::move(replacement), error);
}

bool EditorHudDocument::resetPrefabTarget(std::string_view instanceId,
                                          std::string_view target,
                                          std::string &error) {
  Json replacement = document_.json();
  auto *instance = mutableAuthoredNode(replacement, instanceId);
  if (!instance || !instance->contains("prefab")) {
    error = "Select the owning prefab instance.";
    return false;
  }
  auto pointer = Json::json_pointer(
      composition_.authoredNodes.at(std::string(instanceId)).pointer);
  pointer /= "overrides";
  pointer /= std::string(target);
  pointer /= "children";
  discardOverridesForSource(replacement, pointer.to_string());
  instance = mutableAuthoredNode(replacement, instanceId);
  if (auto overrides = instance->find("overrides");
      overrides != instance->end()) {
    overrides->erase(std::string(target));
    if (overrides->empty())
      instance->erase(overrides);
  }
  return replaceAndRebuild(std::move(replacement), error);
}

} // namespace demi::editor
