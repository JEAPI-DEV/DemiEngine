#include "demi/runtime/scene/RuntimeObjectModel.h"

#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/Transform3DHierarchy.h"
#include "demi/runtime/scene/WorldQueries.h"
#include "demi/runtime/scene/components/EngineComponents.h"

#include <algorithm>
#include <unordered_set>

namespace demi::runtime {
namespace {

using Json = nlohmann::json;
using scene_loading::ComponentDescriptor;

ObjectModelResult failure(std::string error) {
  return {.ok = false, .error = std::move(error)};
}

ObjectModelResult success() { return {.ok = true, .error = {}}; }

std::string validationError(
    const std::vector<scene_loading::ComponentValidationError> &errors) {
  if (errors.empty()) {
    return {};
  }
  return (errors.front().field.empty() ? std::string{}
                                       : errors.front().field + " ") +
         errors.front().message;
}

bool has2DParentCycle(const World &world, const Entity &entity) {
  const auto *transform = entity.component<Transform2DComponent>();
  if (transform == nullptr) {
    return false;
  }
  std::unordered_set<std::string> visited{entity.id};
  std::string parentId = transform->parent;
  while (!parentId.empty()) {
    if (!visited.insert(parentId).second) {
      return true;
    }
    const Entity *parentEntity = findEntity(world, parentId);
    if (parentEntity == nullptr ||
        !parentEntity->hasComponent<Transform2DComponent>()) {
      return true;
    }
    parentId = parentEntity->component<Transform2DComponent>()->parent;
  }
  return false;
}

std::optional<std::string> transformParent(const Entity &entity) {
  if (const auto *transform = entity.component<Transform2DComponent>()) {
    return transform->parent.empty() ? std::nullopt
                                     : std::make_optional(transform->parent);
  }
  if (const auto *transform = entity.component<Transform3DComponent>()) {
    return transform->parent.empty() ? std::nullopt
                                     : std::make_optional(transform->parent);
  }
  return std::nullopt;
}

} // namespace

std::optional<Entity> RuntimeObjectModel::buildEntity(const Json &spec,
                                                      std::string &error) {
  if (!spec.is_object()) {
    error = "entity specification must be an object";
    return std::nullopt;
  }
  Entity entity;
  entity.id = spec.value("id", std::string{});
  if (entity.id.empty()) {
    error = "entity id must not be empty";
    return std::nullopt;
  }
  entity.name = spec.value("name", entity.id);
  entity.enabled = spec.value("enabled", true);
  entity.persistent = spec.value("persistent", false);
  entity.layer = spec.value("layer", std::string{});
  if (const auto tags = spec.find("tags"); tags != spec.end()) {
    if (!tags->is_array() ||
        !std::ranges::all_of(*tags,
                             [](const Json &tag) { return tag.is_string(); })) {
      error = "entity tags must be an array of strings";
      return std::nullopt;
    }
    for (const Json &tag : *tags) {
      entity.tags.insert(tag.get<std::string>());
    }
  }

  const Json components =
      spec.contains("components") ? spec["components"] : Json::object();
  if (!components.is_object()) {
    error = "entity components must be an object";
    return std::nullopt;
  }
  for (const auto &[name, values] : components.items()) {
    const ObjectModelResult result = addComponent(entity, name, values);
    if (!result) {
      error = name + ": " + result.error;
      return std::nullopt;
    }
  }
  return entity;
}

ObjectModelResult RuntimeObjectModel::addEntity(World &world, Entity entity,
                                                const bool replace) {
  if (entity.id.empty()) {
    return failure("entity id must not be empty");
  }
  Entity *existing = findEntity(world, entity.id);
  if (existing != nullptr) {
    if (!replace) {
      return failure("entity already exists: " + entity.id);
    }
    *existing = std::move(entity);
    return success();
  }
  world.entities.push_back(std::move(entity));
  return success();
}

ObjectModelResult RuntimeObjectModel::cloneEntity(
    World &world, const std::string_view sourceId, std::string newId) {
  const Entity *source = findEntity(world, std::string(sourceId));
  if (source == nullptr) {
    return failure("source entity was not found");
  }
  if (findEntity(world, newId) != nullptr) {
    return failure("entity already exists: " + newId);
  }
  Entity clone = *source;
  clone.id = std::move(newId);
  clone.name = clone.id;
  world.entities.push_back(std::move(clone));
  return success();
}

ObjectModelResult RuntimeObjectModel::addComponent(
    Entity &entity, const std::string_view componentName, const Json &values) {
  const ComponentDescriptor *descriptor =
      scene_loading::findComponentDescriptor(componentName);
  if (descriptor == nullptr) {
    return failure("unknown component: " + std::string(componentName));
  }
  if (descriptor->contains(entity)) {
    return failure("component already exists: " + std::string(componentName));
  }
  const auto errors = scene_loading::validateComponent(*descriptor, values);
  if (!errors.empty()) {
    return failure(validationError(errors));
  }
  descriptor->parse(values, entity);
  entity.authoredComponents.push_back(
      scene_loading::makeAuthoredComponent(*descriptor, values.dump()));
  return success();
}

ObjectModelResult
RuntimeObjectModel::removeComponent(Entity &entity,
                                    const std::string_view componentName) {
  const ComponentDescriptor *descriptor =
      scene_loading::findComponentDescriptor(componentName);
  if (descriptor == nullptr) {
    return failure("unknown component: " + std::string(componentName));
  }
  if (!descriptor->remove(entity)) {
    return failure("component was not present: " + std::string(componentName));
  }
  entity.serializedComponents.erase(std::string(componentName));
  std::erase_if(entity.authoredComponents, [&](const auto &component) {
    return component != nullptr && component->name() == componentName;
  });
  return success();
}

bool RuntimeObjectModel::hasComponent(const Entity &entity,
                                      const std::string_view componentName) {
  const ComponentDescriptor *descriptor =
      scene_loading::findComponentDescriptor(componentName);
  return descriptor != nullptr && descriptor->contains(entity);
}

std::optional<Json>
RuntimeObjectModel::componentField(const Entity &entity,
                                   const std::string_view componentName,
                                   const std::string_view fieldName) {
  const ComponentDescriptor *descriptor =
      scene_loading::findComponentDescriptor(componentName);
  if (descriptor == nullptr || !descriptor->contains(entity)) {
    return std::nullopt;
  }
  const auto field =
      std::ranges::find(descriptor->fields, fieldName,
                        &ComponentFieldDescriptor::name);
  if (field == descriptor->fields.end() || !field->luaReadable) {
    return std::nullopt;
  }
  const std::string *serialized =
      serializedComponent(entity, std::string(componentName));
  if (serialized == nullptr) {
    return std::nullopt;
  }
  const Json values = Json::parse(*serialized, nullptr, false);
  if (!values.is_object() || !values.contains(fieldName)) {
    return std::nullopt;
  }
  return values[std::string(fieldName)];
}

ObjectModelResult RuntimeObjectModel::setComponentField(
    Entity &entity, const std::string_view componentName,
    const std::string_view fieldName, const Json &value) {
  const ComponentDescriptor *descriptor =
      scene_loading::findComponentDescriptor(componentName);
  if (descriptor == nullptr || !descriptor->contains(entity)) {
    return failure("component was not found");
  }
  const auto field =
      std::ranges::find(descriptor->fields, fieldName,
                        &ComponentFieldDescriptor::name);
  if (field == descriptor->fields.end()) {
    return failure("field was not found");
  }
  if (!field->luaWritable || field->runtimeReadOnly ||
      field->restartRequired) {
    return failure("field is not runtime writable");
  }

  const std::string componentKey(componentName);
  const auto serialized = entity.serializedComponents.find(componentKey);
  const std::string oldSerialized =
      serialized == entity.serializedComponents.end() ? "{}"
                                                      : serialized->second;
  Json values = Json::parse(oldSerialized, nullptr, false);
  if (!values.is_object()) {
    values = Json::object();
  }
  values[std::string(fieldName)] = value;
  const auto errors = scene_loading::validateComponent(*descriptor, values);
  if (!errors.empty()) {
    return failure(validationError(errors));
  }
  descriptor->parse(values, entity);
  return success();
}

ObjectModelResult RuntimeObjectModel::setComponentField(
    World &world, const std::string_view entityId,
    const std::string_view componentName, const std::string_view fieldName,
    const Json &value) {
  Entity *entity = findEntity(world, std::string(entityId));
  if (entity == nullptr)
    return failure("entity was not found");
  const std::string oldSerialized =
      entity->serializedComponents.contains(std::string(componentName))
          ? entity->serializedComponents.at(std::string(componentName))
          : "{}";
  const ObjectModelResult changed =
      setComponentField(*entity, componentName, fieldName, value);
  if (!changed)
    return changed;

  if (fieldName == "parent" &&
      (componentName == "Transform2D" || componentName == "Transform3D")) {
    const bool invalid2D = componentName == "Transform2D" &&
                           has2DParentCycle(world, *entity);
    const bool invalid3D = componentName == "Transform3D" &&
                           !validateTransform3DHierarchy(world).empty();
    if (invalid2D || invalid3D) {
      const ComponentDescriptor *descriptor =
          scene_loading::findComponentDescriptor(componentName);
      descriptor->parse(Json::parse(oldSerialized), *entity);
      return failure("parent would create a cycle or reference an invalid "
                     "parent");
    }
  }
  return success();
}

std::vector<std::string> RuntimeObjectModel::query(
    const World &world, const EntityQuery &querySpec) {
  std::vector<std::string> result;
  for (const Entity &entity : world.entities) {
    if (!querySpec.includeDisabled && !entity.enabled) {
      continue;
    }
    if (querySpec.layer.has_value() && entity.layer != *querySpec.layer) {
      continue;
    }
    if (!std::ranges::all_of(querySpec.tags, [&](const std::string &tag) {
          return entity.tags.contains(tag);
        }) ||
        !std::ranges::all_of(
            querySpec.allComponents, [&](const std::string &component) {
              return hasComponent(entity, component);
            })) {
      continue;
    }
    result.push_back(entity.id);
  }
  return result;
}

ObjectModelResult RuntimeObjectModel::setParent(
    World &world, const std::string_view entityId,
    const std::optional<std::string> parentId) {
  Entity *entity = findEntity(world, std::string(entityId));
  if (entity == nullptr) {
    return failure("entity was not found");
  }
  const std::string component =
      entity->hasComponent<Transform2DComponent>() ? "Transform2D"
      : entity->hasComponent<Transform3DComponent>() ? "Transform3D"
                                                     : std::string{};
  if (component.empty()) {
    return failure("entity has no Transform2D or Transform3D");
  }
  return setComponentField(world, entityId, component, "parent",
                           parentId.value_or(std::string{}));
}

std::optional<std::string>
RuntimeObjectModel::parent(const World &world, const std::string_view entityId) {
  const Entity *entity = findEntity(world, std::string(entityId));
  return entity == nullptr ? std::nullopt : transformParent(*entity);
}

std::vector<std::string>
RuntimeObjectModel::children(const World &world,
                             const std::string_view entityId) {
  std::vector<std::string> result;
  for (const Entity &entity : world.entities) {
    if (transformParent(entity) == entityId) {
      result.push_back(entity.id);
    }
  }
  return result;
}

std::optional<Json>
RuntimeObjectModel::localPosition(const World &world,
                                  const std::string_view entityId) {
  const Entity *entity = findEntity(world, std::string(entityId));
  if (entity == nullptr) {
    return std::nullopt;
  }
  if (const auto *transform = entity->component<Transform2DComponent>()) {
    return Json::array({transform->position.x, transform->position.y});
  }
  if (const auto *transform = entity->component<Transform3DComponent>()) {
    return Json::array(
        {transform->position.x, transform->position.y, transform->position.z});
  }
  return std::nullopt;
}

std::optional<Json>
RuntimeObjectModel::worldPosition(const World &world,
                                  const std::string_view entityId) {
  const Entity *entity = findEntity(world, std::string(entityId));
  if (entity == nullptr) {
    return std::nullopt;
  }
  if (entity->hasComponent<Transform2DComponent>()) {
    const Vec2 position = worldPosition2D(world, *entity);
    return Json::array({position.x, position.y});
  }
  if (entity->hasComponent<Transform3DComponent>()) {
    const Vec3 position = worldPosition3D(world, *entity);
    return Json::array({position.x, position.y, position.z});
  }
  return std::nullopt;
}

} // namespace demi::runtime
