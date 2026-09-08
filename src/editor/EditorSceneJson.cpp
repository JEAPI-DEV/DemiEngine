#include "editor/EditorSceneJson.h"

#include <algorithm>

namespace demi::editor {
namespace {

nlohmann::json *findInstance(nlohmann::json &document,
                             const std::string_view id) {
  auto instances = document.find("instances");
  if (instances == document.end() || !instances->is_array())
    return nullptr;
  const auto found = std::ranges::find_if(*instances, [&](auto &instance) {
    return instance.is_object() && instance.value("id", std::string{}) == id;
  });
  return found == instances->end() ? nullptr : &*found;
}

const nlohmann::json *findInstance(const nlohmann::json &document,
                                   const std::string_view id) {
  auto instances = document.find("instances");
  if (instances == document.end() || !instances->is_array())
    return nullptr;
  const auto found =
      std::ranges::find_if(*instances, [&](const auto &instance) {
        return instance.is_object() &&
               instance.value("id", std::string{}) == id;
      });
  return found == instances->end() ? nullptr : &*found;
}

std::string flattenedOverrideKey(const SceneValueTarget &target) {
  std::string key = target.prefabEntityId;
  if (!target.component.empty())
    key += "." + target.component;
  return key + "." + target.field;
}

template <typename Json>
Json *prefabOverrideValue(Json &document, const SceneValueTarget &target) {
  Json *instance = findInstance(document, target.prefabInstanceId);
  if (instance == nullptr)
    return nullptr;
  auto overrides = instance->find("overrides");
  if (overrides == instance->end() || !overrides->is_object())
    return nullptr;
  const std::string flattened = flattenedOverrideKey(target);
  if (auto value = overrides->find(flattened); value != overrides->end())
    return &*value;
  auto local = overrides->find(target.prefabEntityId);
  if (local == overrides->end() || !local->is_object())
    return nullptr;
  Json *container = &*local;
  if (!target.component.empty()) {
    auto components = container->find("components");
    if (components == container->end() || !components->is_object())
      return nullptr;
    auto component = components->find(target.component);
    if (component == components->end() || !component->is_object())
      return nullptr;
    container = &*component;
  }
  const auto value = container->find(target.field);
  return value == container->end() ? nullptr : &*value;
}

void pruneEmptyPrefabOverride(nlohmann::json &instance,
                              const SceneValueTarget &target) {
  auto overrides = instance.find("overrides");
  if (overrides == instance.end() || !overrides->is_object())
    return;
  auto local = overrides->find(target.prefabEntityId);
  if (local != overrides->end() && local->is_object() &&
      !target.component.empty()) {
    auto components = local->find("components");
    if (components != local->end() && components->is_object()) {
      auto component = components->find(target.component);
      if (component != components->end() && component->is_object() &&
          component->empty())
        components->erase(component);
      if (components->empty())
        local->erase(components);
    }
  }
  local = overrides->find(target.prefabEntityId);
  if (local != overrides->end() && local->is_object() && local->empty())
    overrides->erase(local);
  if (overrides->empty())
    instance.erase(overrides);
}

} // namespace

nlohmann::json *findEntity(nlohmann::json &document,
                           const std::string_view id) {
  auto entities = document.find("entities");
  if (entities == document.end() || !entities->is_array())
    return nullptr;
  const auto found = std::ranges::find_if(*entities, [&](auto &entity) {
    return entity.is_object() && entity.value("id", std::string{}) == id;
  });
  return found == entities->end() ? nullptr : &*found;
}

const nlohmann::json *findEntity(const nlohmann::json &document,
                                 const std::string_view id) {
  auto entities = document.find("entities");
  if (entities == document.end() || !entities->is_array())
    return nullptr;
  const auto found = std::ranges::find_if(*entities, [&](const auto &entity) {
    return entity.is_object() && entity.value("id", std::string{}) == id;
  });
  return found == entities->end() ? nullptr : &*found;
}

nlohmann::json *entitiesArray(nlohmann::json &document) {
  auto entities = document.find("entities");
  return entities == document.end() || !entities->is_array() ? nullptr
                                                             : &*entities;
}

const nlohmann::json *entitiesArray(const nlohmann::json &document) {
  auto entities = document.find("entities");
  return entities == document.end() || !entities->is_array() ? nullptr
                                                             : &*entities;
}

std::optional<std::size_t> entityIndex(const nlohmann::json &document,
                                       const std::string_view id) {
  const nlohmann::json *entities = entitiesArray(document);
  if (entities == nullptr)
    return std::nullopt;
  const auto found = std::ranges::find_if(*entities, [&](const auto &entity) {
    return entity.is_object() && entity.value("id", std::string{}) == id;
  });
  if (found == entities->end())
    return std::nullopt;
  return static_cast<std::size_t>(std::distance(entities->begin(), found));
}

nlohmann::json *findComponent(nlohmann::json &entity,
                              const std::string_view name) {
  auto components = entity.find("components");
  nlohmann::json &source = components != entity.end() && components->is_object()
                               ? *components
                               : entity;
  auto component = source.find(name);
  return component == source.end() || !component->is_object() ? nullptr
                                                              : &*component;
}

const nlohmann::json *findComponent(const nlohmann::json &entity,
                                    const std::string_view name) {
  auto components = entity.find("components");
  const nlohmann::json &source =
      components != entity.end() && components->is_object() ? *components
                                                            : entity;
  auto component = source.find(name);
  return component == source.end() || !component->is_object() ? nullptr
                                                              : &*component;
}

nlohmann::json *valueInDocument(nlohmann::json &document,
                                const SceneValueTarget &target) {
  if (target.isPrefabOverride())
    return prefabOverrideValue(document, target);
  nlohmann::json *authoredEntity = findEntity(document, target.entityId);
  if (authoredEntity == nullptr)
    return nullptr;
  nlohmann::json *container = authoredEntity;
  if (!target.component.empty()) {
    container = findComponent(*authoredEntity, target.component);
    if (container == nullptr)
      return nullptr;
  }
  const auto field = container->find(target.field);
  return field == container->end() ? nullptr : &*field;
}

const nlohmann::json *valueInDocument(const nlohmann::json &document,
                                      const SceneValueTarget &target) {
  if (target.isPrefabOverride())
    return prefabOverrideValue(document, target);
  const nlohmann::json *authoredEntity = findEntity(document, target.entityId);
  if (authoredEntity == nullptr)
    return nullptr;
  const nlohmann::json *container = authoredEntity;
  if (!target.component.empty()) {
    container = findComponent(*authoredEntity, target.component);
    if (container == nullptr)
      return nullptr;
  }
  const auto field = container->find(target.field);
  return field == container->end() ? nullptr : &*field;
}

bool assignValueInDocument(nlohmann::json &document,
                           const SceneValueTarget &target,
                           const std::optional<nlohmann::json> &value) {
  if (target.isPrefabOverride()) {
    nlohmann::json *instance = findInstance(document, target.prefabInstanceId);
    if (instance == nullptr || target.prefabEntityId.empty())
      return false;
    nlohmann::json &overrides = (*instance)["overrides"];
    if (!overrides.is_object())
      overrides = nlohmann::json::object();
    const std::string flattened = flattenedOverrideKey(target);
    if (overrides.contains(flattened)) {
      if (value)
        overrides[flattened] = *value;
      else
        overrides.erase(flattened);
    } else if (value) {
      nlohmann::json &local = overrides[target.prefabEntityId];
      if (!local.is_object())
        local = nlohmann::json::object();
      nlohmann::json *container = &local;
      if (!target.component.empty()) {
        nlohmann::json &components = local["components"];
        if (!components.is_object())
          components = nlohmann::json::object();
        nlohmann::json &component = components[target.component];
        if (!component.is_object())
          component = nlohmann::json::object();
        container = &component;
      }
      (*container)[target.field] = *value;
    } else {
      nlohmann::json *current = prefabOverrideValue(document, target);
      if (current == nullptr)
        return false;
      nlohmann::json &local = overrides[target.prefabEntityId];
      nlohmann::json *container = &local;
      if (!target.component.empty())
        container = &local["components"][target.component];
      container->erase(target.field);
    }
    pruneEmptyPrefabOverride(*instance, target);
    return true;
  }
  nlohmann::json *container = findEntity(document, target.entityId);
  if (container == nullptr)
    return false;
  if (!target.component.empty()) {
    container = findComponent(*container, target.component);
    if (container == nullptr)
      return false;
  }
  if (value.has_value())
    (*container)[target.field] = *value;
  else
    container->erase(target.field);
  return true;
}

std::string transformParentId(const nlohmann::json &entity) {
  const auto components = entity.find("components");
  if (components == entity.end() || !components->is_object())
    return {};
  for (const char *name : {"Transform3D", "Transform2D", "IsoTransform"}) {
    const auto transform = components->find(name);
    if (transform == components->end() || !transform->is_object())
      continue;
    const auto parent = transform->find("parent");
    if (parent != transform->end() && parent->is_string())
      return parent->get<std::string>();
  }
  return {};
}

const char *transformComponentName(const nlohmann::json &entity) {
  const auto components = entity.find("components");
  if (components == entity.end() || !components->is_object())
    return nullptr;
  for (const char *name : {"Transform3D", "Transform2D", "IsoTransform"}) {
    if (components->contains(name))
      return name;
  }
  return nullptr;
}

std::vector<std::string> collectSubtreeIds(const nlohmann::json &document,
                                           const std::string_view rootId) {
  std::vector<std::string> result{std::string(rootId)};
  std::unordered_set<std::string> visited{std::string(rootId)};
  for (std::size_t cursor = 0; cursor < result.size(); ++cursor) {
    const nlohmann::json *entities = entitiesArray(document);
    if (entities == nullptr)
      break;
    for (const auto &entity : *entities) {
      if (!entity.is_object())
        continue;
      const std::string id = entity.value("id", std::string{});
      if (!id.empty() && transformParentId(entity) == result[cursor] &&
          visited.insert(id).second)
        result.push_back(id);
    }
  }
  return result;
}

std::string uniqueEntityId(const nlohmann::json &document,
                           const std::string_view base,
                           const std::unordered_set<std::string> &reserved) {
  const auto taken = [&](const std::string &candidate) {
    return reserved.contains(candidate) ||
           findEntity(document, candidate) != nullptr;
  };
  std::string candidate(base);
  std::size_t suffix = 2;
  while (taken(candidate)) {
    candidate = std::string(base) + "_" + std::to_string(suffix++);
  }
  return candidate;
}

void remapParentReferences(
    nlohmann::json &entity,
    const std::unordered_map<std::string, std::string> &remap) {
  auto components = entity.find("components");
  if (components == entity.end() || !components->is_object())
    return;
  for (const char *name : {"Transform3D", "Transform2D"}) {
    auto transform = components->find(name);
    if (transform == components->end() || !transform->is_object())
      continue;
    auto parent = transform->find("parent");
    if (parent == transform->end() || !parent->is_string())
      continue;
    const auto remapped = remap.find(parent->get<std::string>());
    if (remapped != remap.end())
      *parent = remapped->second;
  }
}

} // namespace demi::editor
