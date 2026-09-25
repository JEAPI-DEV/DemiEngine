#include "editor/EditorPrefabAuthoring.h"

#include "editor/EditorSceneJson.h"

#include <algorithm>
#include <exception>
#include <unordered_set>
#include <vector>

namespace demi::editor {
namespace {

using Json = nlohmann::json;

const Json *legacyInstance(const Json &scene, const std::string_view id) {
  const auto instances = scene.find("instances");
  if (instances == scene.end() || !instances->is_array())
    return nullptr;
  const auto found = std::ranges::find_if(*instances, [&](const Json &item) {
    return item.is_object() && item.value("id", std::string{}) == id &&
           item.contains("prefab") && item["prefab"].is_string();
  });
  return found == instances->end() ? nullptr : &*found;
}

void collectInlinePrefabInstances(const Json &entities,
                                  std::vector<const Json *> &instances) {
  if (!entities.is_array())
    return;
  for (const Json &entity : entities) {
    if (!entity.is_object())
      continue;
    if (entity.contains("prefab") && entity["prefab"].is_string())
      instances.push_back(&entity);
    if (const auto children = entity.find("children");
        children != entity.end())
      collectInlinePrefabInstances(*children, instances);
  }
}

bool ownsExpandedId(const Json &instance, const std::string_view selectedId) {
  const std::string instanceId = instance.value("id", std::string{});
  return !instanceId.empty() && selectedId.size() > instanceId.size() &&
         selectedId.starts_with(instanceId) &&
         selectedId[instanceId.size()] == '/';
}

const Json *owningAuthoredInstance(const Json &scene,
                                   const std::string_view selectedId) {
  std::vector<const Json *> candidates;
  if (const Json *entities = entitiesArray(scene))
    collectInlinePrefabInstances(*entities, candidates);
  if (const auto instances = scene.find("instances");
      instances != scene.end() && instances->is_array()) {
    for (const Json &instance : *instances)
      if (instance.is_object() && instance.contains("prefab") &&
          instance["prefab"].is_string())
        candidates.push_back(&instance);
  }

  const Json *owner = nullptr;
  for (const Json *candidate : candidates) {
    if (!ownsExpandedId(*candidate, selectedId))
      continue;
    if (owner == nullptr ||
        candidate->value("id", std::string{}).size() >
            owner->value("id", std::string{}).size())
      owner = candidate;
  }
  return owner;
}

void collectNestedIds(const Json &entity,
                      std::unordered_set<std::string> &ids) {
  const std::string id = entity.value("id", std::string{});
  if (!id.empty())
    ids.insert(id);
  if (const auto children = entity.find("children");
      children != entity.end() && children->is_array())
    for (const Json &child : *children)
      if (child.is_object())
        collectNestedIds(child, ids);
}

void detachExternalRootParent(Json &root,
                              const std::unordered_set<std::string> &subtree) {
  const std::string parent = transformParentId(root);
  if (parent.empty() || subtree.contains(parent))
    return;
  for (const char *name : {"Transform3D", "Transform2D", "IsoTransform"}) {
    Json *transform = findComponent(root, name);
    if (transform != nullptr)
      transform->erase("parent");
  }
}

std::optional<Json> copyAuthoredSubtree(const Json &scene, const Json &source,
                                        const std::string_view rootId,
                                        std::string &error) {
  std::vector<std::string> subtreeIds;
  try {
    subtreeIds = collectSubtreeIds(scene, rootId);
  } catch (const std::exception &exception) {
    error = "The selected authored hierarchy is invalid: " +
            std::string(exception.what());
    return std::nullopt;
  }
  const std::unordered_set<std::string> subtree(subtreeIds.begin(),
                                                 subtreeIds.end());

  Json root = source;
  detachExternalRootParent(root, subtree);
  Json copied = Json::array({std::move(root)});

  std::unordered_set<std::string> alreadyCopied;
  collectNestedIds(copied.front(), alreadyCopied);
  const Json *entities = entitiesArray(scene);
  if (entities == nullptr)
    return copied;
  for (const Json &entity : *entities) {
    if (!entity.is_object())
      continue;
    const std::string id = entity.value("id", std::string{});
    if (id.empty() || !subtree.contains(id) || alreadyCopied.contains(id))
      continue;
    copied.push_back(entity);
    collectNestedIds(entity, alreadyCopied);
  }
  return copied;
}

} // namespace

std::optional<nlohmann::json>
makeEntityPrefab(const nlohmann::json &scene, const std::string_view selectedId,
                 const std::string_view prefabId, std::string &error) {
  error.clear();
  if (!scene.is_object() || entitiesArray(scene) == nullptr) {
    error = "Prefab export requires a scene with an entities array.";
    return std::nullopt;
  }
  if (selectedId.empty()) {
    error = "Select an authored entity or prefab instance to export.";
    return std::nullopt;
  }
  constexpr std::string_view Prefix = "prefab://";
  if (!prefabId.starts_with(Prefix) || prefabId.size() == Prefix.size()) {
    error = "Entity prefab IDs must use a nonempty prefab:// reference.";
    return std::nullopt;
  }

  const Json *source = findEntity(scene, selectedId);
  if (source == nullptr)
    source = legacyInstance(scene, selectedId);
  if (source == nullptr)
    source = owningAuthoredInstance(scene, selectedId);
  if (source == nullptr) {
    error = "The selection is not an authored entity. Expanded prefab children "
            "cannot be baked or unpacked; select their authored instance root "
            "or open the source prefab.";
    return std::nullopt;
  }

  const std::string sourceId = source->value("id", std::string{});
  if (sourceId.empty()) {
    error = "The selected authored entity has no stable ID.";
    return std::nullopt;
  }
  auto entities = copyAuthoredSubtree(scene, *source, sourceId, error);
  if (!entities)
    return std::nullopt;
  return Json{{"format_version", 1},
              {"id", std::string(prefabId)},
              {"entities", std::move(*entities)}};
}

} // namespace demi::editor
