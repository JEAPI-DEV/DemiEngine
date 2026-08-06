#include "demi/runtime/scene/SceneEntityParser.h"

#include "demi/runtime/scene/ComponentRegistry.h"

namespace demi::runtime::scene_loading {

namespace {

void parseComponents(const Json &entityJson, Entity &entity) {
  const Json *components = objectField(entityJson, "components");
  const Json &componentSource =
      components != nullptr ? *components : entityJson;

  for (auto iterator = componentSource.begin();
       iterator != componentSource.end(); ++iterator) {
    if (!iterator.value().is_object()) {
      continue;
    }

    const std::string serialized = iterator.value().dump();
    const ComponentDescriptor *descriptor =
        findComponentDescriptor(iterator.key());
    if (descriptor == nullptr) {
      continue;
    }

    entity.authoredComponents.push_back(
        makeAuthoredComponent(*descriptor, serialized));
    descriptor->parse(iterator.value(), entity);
  }
}

Entity parseEntity(const Json &entityJson) {
  Entity entity;
  entity.id = stringOr(entityJson, "id", "ent_unknown");
  entity.name = stringOr(entityJson, "name", entity.id);
  entity.enabled = boolField(entityJson, "enabled").value_or(true);
  entity.persistent = boolField(entityJson, "persistent").value_or(false);
  entity.layer = stringOr(entityJson, "layer");
  if (const Json *tags = arrayField(entityJson, "tags")) {
    for (const Json &tag : *tags) {
      if (tag.is_string()) {
        entity.tags.insert(tag.get<std::string>());
      }
    }
  }
  parseComponents(entityJson, entity);
  return entity;
}

} // namespace

World parseSceneWorld(const std::filesystem::path &scenePath,
                      const Json &document) {
  World world;
  world.scenePath = scenePath;
  world.id = stringOr(document, "id", "scene://unknown");
  world.name = stringOr(document, "name", world.id);

  const Json *entities = arrayField(document, "entities");
  if (entities == nullptr) {
    return world;
  }

  for (const Json &entityJson : *entities) {
    if (entityJson.is_object()) {
      world.entities.push_back(parseEntity(entityJson));
    }
  }
  return world;
}

} // namespace demi::runtime::scene_loading
