#include "editor/EditorAuthoringClipboard.h"

#include "demi/runtime/scene/ComponentRegistry.h"

#include <functional>

namespace demi::editor {
namespace {

using Json = nlohmann::json;

bool collectTree(const Json &node, std::vector<std::string> &ids,
                 std::set<std::string> &seen, std::string &error) {
  if (!node.is_object() || !node.contains("id") || !node["id"].is_string() ||
      node["id"].get_ref<const std::string &>().empty()) {
    error = "Every clipboard element needs a nonempty stable string ID.";
    return false;
  }
  const auto id = node["id"].get<std::string>();
  if (!seen.insert(id).second) {
    error = "Duplicate clipboard ID: " + id;
    return false;
  }
  ids.push_back(id);
  if (const auto mapping = node.find("entity_ids"); mapping != node.end()) {
    if (!node.contains("prefab") || !mapping->is_object()) {
      error = "entity_ids requires a prefab instance and an object.";
      return false;
    }
    for (const auto &target : *mapping) {
      if (!target.is_string() ||
          target.get_ref<const std::string &>().empty() ||
          !seen.insert(target.get<std::string>()).second) {
        error = "Prefab identity mappings must use unique nonempty IDs.";
        return false;
      }
      ids.push_back(target.get<std::string>());
    }
  }
  for (const char *field : {"children", "elements"}) {
    const auto children = node.find(field);
    if (children == node.end())
      continue;
    if (!children->is_array()) {
      error = "Clipboard children must be an array.";
      return false;
    }
    for (const auto &child : *children)
      if (!collectTree(child, ids, seen, error))
        return false;
  }
  return true;
}

void visitTrees(Json &roots, const std::function<void(Json &)> &visit) {
  for (auto &node : roots) {
    visit(node);
    for (const char *field : {"children", "elements"})
      if (auto children = node.find(field); children != node.end())
        visitTrees(*children, visit);
  }
}

void remapReference(Json &value, const EditorClipboardIdMap &ids,
                    const std::set<std::string> &instances = {}) {
  if (value.is_string()) {
    const auto source = value.get<std::string>();
    if (const auto found = ids.find(source); found != ids.end()) {
      value = found->second;
      return;
    }
    for (const auto &instance : instances) {
      const std::string prefix = instance + '/';
      if (source.starts_with(prefix)) {
        value = ids.at(instance) + source.substr(instance.size());
        return;
      }
    }
  } else if (value.is_array()) {
    for (auto &entry : value)
      remapReference(entry, ids, instances);
  }
}

void remapComponents(Json &entity, const EditorClipboardIdMap &ids,
                     const std::set<std::string> &instances) {
  auto components = entity.find("components");
  Json &values = components == entity.end() ? entity : *components;
  if (!values.is_object())
    return;
  for (const auto &descriptor :
       runtime::scene_loading::componentDescriptors()) {
    auto component = values.find(descriptor.name);
    if (component == values.end() || !component->is_object())
      continue;
    for (const auto &field : descriptor.fields) {
      if (field.referenceKind != runtime::ComponentReferenceKind::Entity)
        continue;
      if (auto value = component->find(field.name); value != component->end())
        remapReference(*value, ids, instances);
    }
  }
}

void remapOverrides(Json &entity, const EditorClipboardIdMap &ids,
                    const std::set<std::string> &instances) {
  auto overrides = entity.find("overrides");
  if (overrides == entity.end() || !overrides->is_object())
    return;
  for (auto &[key, value] : overrides->items()) {
    // Nested local-entity override keys are prefab-local identities, not IDs
    // in the scene. Their typed reference values may refer to copied objects.
    if (value.is_object())
      remapComponents(value, ids, instances);
    for (const auto &descriptor :
         runtime::scene_loading::componentDescriptors())
      if (value.is_object() &&
          key.ends_with('.' + std::string(descriptor.name))) {
        Json wrapper{{"components", {{std::string(descriptor.name), value}}}};
        remapComponents(wrapper, ids, instances);
        value = std::move(wrapper["components"][descriptor.name]);
      }
    for (const auto &descriptor :
         runtime::scene_loading::componentDescriptors())
      for (const auto &field : descriptor.fields) {
        if (field.referenceKind != runtime::ComponentReferenceKind::Entity)
          continue;
        const std::string suffix =
            '.' + std::string(descriptor.name) + '.' + std::string(field.name);
        if (key.ends_with(suffix))
          remapReference(value, ids, instances);
      }
  }
}

} // namespace

bool collectClipboardTreeIds(const Json &roots, std::vector<std::string> &ids,
                             std::string &error) {
  ids.clear();
  if (!roots.is_array() || roots.empty()) {
    error = "The clipboard must contain at least one authored element.";
    return false;
  }
  std::set<std::string> seen;
  for (const auto &node : roots)
    if (!collectTree(node, ids, seen, error))
      return false;
  return true;
}

EditorClipboardIdMap allocateClipboardIds(const std::vector<std::string> &ids,
                                          std::set<std::string> reserved) {
  EditorClipboardIdMap result;
  for (const auto &id : ids) {
    const std::string base = id + "_copy";
    std::string candidate = base;
    std::size_t suffix = 2;
    while (!reserved.insert(candidate).second)
      candidate = base + '_' + std::to_string(suffix++);
    result.emplace(id, std::move(candidate));
  }
  return result;
}

void remapClipboardEntities(Json &roots, const EditorClipboardIdMap &ids) {
  std::set<std::string> instances;
  visitTrees(roots, [&](Json &node) {
    if (node.contains("prefab"))
      instances.insert(node["id"].get<std::string>());
  });
  visitTrees(roots, [&](Json &node) {
    node["id"] = ids.at(node["id"].get<std::string>());
    if (auto mapping = node.find("entity_ids"); mapping != node.end())
      for (auto &target : *mapping)
        remapReference(target, ids);
    remapComponents(node, ids, instances);
    remapOverrides(node, ids, instances);
  });
}

void remapClipboardHudNodes(Json &roots, const EditorClipboardIdMap &ids) {
  visitTrees(roots, [&](Json &node) {
    node["id"] = ids.at(node["id"].get<std::string>());
    if (auto parent = node.find("parent"); parent != node.end())
      remapReference(*parent, ids);
  });
}

} // namespace demi::editor
