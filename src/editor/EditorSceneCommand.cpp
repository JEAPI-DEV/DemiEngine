#include "editor/EditorSceneCommand.h"

#include <type_traits>

namespace demi::editor {
namespace {
std::string terrainRecipeOverrideKey(const SceneValueTarget &target) {
  return target.prefabEntityId + "." + target.component + "." + target.field;
}

template <typename Json>
Json *nestedTerrainRecipe(Json &overrides, const SceneValueTarget &target) {
  auto entity = overrides.find(target.prefabEntityId);
  if (entity == overrides.end() || !entity->is_object())
    return nullptr;
  auto components = entity->find("components");
  if (components == entity->end() || !components->is_object())
    return nullptr;
  auto component = components->find(target.component);
  if (component == components->end() || !component->is_object())
    return nullptr;
  auto recipe = component->find(target.field);
  return recipe == component->end() ? nullptr : &*recipe;
}

nlohmann::json terrainOverrideShape(const nlohmann::json &document,
                                    const SceneValueTarget &target) {
  const auto *instance = findPrefabInstance(document, target.prefabInstanceId);
  if (instance == nullptr || !instance->contains("overrides"))
    return nullptr;
  auto shape = instance->at("overrides");
  const auto dotted = shape.find(terrainRecipeOverrideKey(target));
  if (dotted != shape.end())
    *dotted = nullptr;
  if (auto *nested = nestedTerrainRecipe(shape, target))
    *nested = nullptr;
  return shape;
}

void applyTerrainRecipe(nlohmann::json &document,
                        const TerrainRecipeCommand &command, bool forward) {
  const auto *current = valueInDocument(document, command.target);
  const nlohmann::json recipe =
      (current ? *current : nlohmann::json())
          .patch(forward ? command.forwardPatch : command.inversePatch);
  if (forward) {
    (void)assignValueInDocument(document, command.target, recipe);
    return;
  }
  if (!command.target.isPrefabOverride()) {
    (void)assignValueInDocument(document, command.target,
                                command.hadRecipe ? std::optional(recipe)
                                                  : std::nullopt);
    return;
  }
  auto *instance =
      findPrefabInstance(document, command.target.prefabInstanceId);
  if (instance == nullptr)
    return;
  auto overrides =
      instance->at("overrides").patch(command.prefabSourceInversePatch);
  if (overrides.is_null()) {
    instance->erase("overrides");
    return;
  }
  const auto dotted = overrides.find(terrainRecipeOverrideKey(command.target));
  if (dotted != overrides.end())
    *dotted = recipe;
  if (auto *nested = nestedTerrainRecipe(overrides, command.target))
    *nested = command.prefabNestedRecipePatch
                  ? recipe.patch(*command.prefabNestedRecipePatch)
                  : recipe;
  (*instance)["overrides"] = std::move(overrides);
}

void applyValue(nlohmann::json &document, const SetValueCommand &command, bool forward) {
  if (!forward && command.target.isPrefabOverride()) {
    if (auto *instance =
            findPrefabInstance(document, command.target.prefabInstanceId)) {
      if (command.prefabOverridesBefore)
        (*instance)["overrides"] = *command.prefabOverridesBefore;
      else
        instance->erase("overrides");
    }
    return;
  }
  if (!forward && command.createdComponent) {
    if (auto *entity = findEntity(document, command.target.entityId)) {
      if (command.createdComponentsContainer)
        entity->erase("components");
      else
        (*entity)["components"].erase(command.target.component);
    }
    return;
  }
  (void)assignValueInDocument(document, command.target,
                              forward ? command.after : command.before);
  if (forward && command.preserveEmptyPrefabComponent) {
    auto *instance =
        findPrefabInstance(document, command.target.prefabInstanceId);
    if (instance != nullptr) {
      auto &component = (*instance)["overrides"][command.target.prefabEntityId]
                                   ["components"][command.target.component];
      if (component.is_null())
        component = nlohmann::json::object();
    }
  }
}
} // namespace

TerrainRecipeCommand makeTerrainRecipeCommand(
    const nlohmann::json &before, const nlohmann::json &after,
    SceneValueTarget target,
    std::shared_ptr<const runtime::TerrainPatch> samplePatch) {
  const auto *oldRecipe = valueInDocument(before, target);
  const auto &newRecipe = *valueInDocument(after, target);
  const nlohmann::json baseline = oldRecipe ? *oldRecipe : nlohmann::json();
  TerrainRecipeCommand command{
      .target = std::move(target),
      .forwardPatch = nlohmann::json::diff(baseline, newRecipe),
      .inversePatch = nlohmann::json::diff(newRecipe, baseline),
      .samplePatch = std::move(samplePatch),
      .hadRecipe = oldRecipe != nullptr};
  if (command.target.isPrefabOverride()) {
    command.prefabSourceInversePatch =
        nlohmann::json::diff(terrainOverrideShape(after, command.target),
                             terrainOverrideShape(before, command.target));
    const auto *instance =
        findPrefabInstance(before, command.target.prefabInstanceId);
    if (instance && instance->contains("overrides")) {
      if (const auto *nested =
              nestedTerrainRecipe(instance->at("overrides"), command.target);
          nested != nullptr && *nested != baseline)
        command.prefabNestedRecipePatch =
            nlohmann::json::diff(baseline, *nested);
    }
  }
  return command;
}

void applySceneCommand(nlohmann::json &document, const SceneCommand &command,
                       const bool forward) {
  using Difference = nlohmann::json::difference_type;
  std::visit(
      [&](const auto &typed) {
        using Command = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<Command, SetSceneHudCommand>) {
          const auto &value = forward ? typed.after : typed.before;
          if (value) document["hud"] = *value;
          else document.erase("hud");
        } else if constexpr (std::is_same_v<Command, SetValueCommand>) {
          applyValue(document, typed, forward);
        } else if constexpr (std::is_same_v<Command, TerrainRecipeCommand>) {
          applyTerrainRecipe(document, typed, forward);
        } else if constexpr (std::is_same_v<Command, SetValuesCommand>) {
          if (forward) {
            for (const SetValueCommand &value : typed.values)
              applyValue(document, value, true);
          } else {
            for (auto value = typed.values.rbegin();
                 value != typed.values.rend(); ++value)
              applyValue(document, *value, false);
          }
        } else if constexpr (std::is_same_v<Command, InsertEntityCommand>) {
          nlohmann::json *entities = entitiesArray(document);
          if (entities == nullptr)
            return;
          const auto position =
              entities->begin() + static_cast<Difference>(typed.index);
          if (forward)
            entities->insert(position, typed.entity);
          else
            entities->erase(position);
        } else if constexpr (std::is_same_v<Command, RemoveEntitiesCommand>) {
          nlohmann::json *entities = entitiesArray(document);
          if (entities == nullptr)
            return;
          if (forward) {
            for (auto item = typed.entities.rbegin();
                 item != typed.entities.rend(); ++item) {
              const auto position =
                  entities->begin() + static_cast<Difference>(item->index);
              entities->erase(position);
            }
          } else {
            for (const IndexedSceneEntity &item : typed.entities) {
              const auto position =
                  entities->begin() + static_cast<Difference>(item.index);
              entities->insert(position, item.entity);
            }
          }
        } else if constexpr (std::is_same_v<Command, DuplicateEntityCommand>) {
          nlohmann::json *entities = entitiesArray(document);
          if (entities == nullptr)
            return;
          auto position =
              entities->begin() + static_cast<Difference>(typed.index);
          if (forward) {
            for (const nlohmann::json &copy : typed.entities)
              position = entities->insert(position, copy) + 1;
          } else {
            entities->erase(position, position + static_cast<Difference>(
                                                     typed.entities.size()));
          }
        } else if constexpr (std::is_same_v<Command, EntityHierarchyCommand>) {
          document["entities"] = forward ? typed.after : typed.before;
          if (typed.instancesBefore || typed.instancesAfter) {
            const auto &instances = forward ? typed.instancesAfter : typed.instancesBefore;
            if (instances)
              document["instances"] = *instances;
            else
              document.erase("instances");
          }
        } else if constexpr (std::is_same_v<Command, ReparentCommand>) {
          nlohmann::json *entity = findEntity(document, typed.entityId);
          nlohmann::json *transform =
              entity == nullptr ? nullptr
                                : findComponent(*entity, typed.component);
          if (transform == nullptr)
            return;
          const auto &parent = forward ? typed.after : typed.before;
          if (parent.has_value())
            (*transform)["parent"] = *parent;
          else
            transform->erase("parent");
        } else if constexpr (std::is_same_v<Command,
                                            SetPrefabOverridesCommand>) {
          auto *instance = findPrefabInstance(document, typed.instanceId);
          if (instance == nullptr)
            return;
          const auto &value = forward ? typed.after : typed.before;
          if (value)
            (*instance)["overrides"] = *value;
          else
            instance->erase("overrides");
        } else if constexpr (std::is_same_v<Command, AddComponentCommand>) {
          nlohmann::json *entity = findEntity(document, typed.entityId);
          if (entity == nullptr)
            return;
          nlohmann::json &components = (*entity)["components"];
          if (forward)
            components[typed.componentName] = typed.component;
          else
            components.erase(typed.componentName);
        } else if constexpr (std::is_same_v<Command, RemoveComponentCommand>) {
          nlohmann::json *entity = findEntity(document, typed.entityId);
          if (entity == nullptr)
            return;
          nlohmann::json &components = (*entity)["components"];
          if (forward)
            components.erase(typed.componentName);
          else
            components[typed.componentName] = typed.component;
        }
      },
      command);
}

std::string sceneCommandEntityId(const SceneCommand &command) {
  return std::visit(
      [](const auto &typed) -> std::string {
        using Command = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<Command, SetSceneHudCommand>)
          return {};
        else if constexpr (std::is_same_v<Command, SetValueCommand> ||
                           std::is_same_v<Command, TerrainRecipeCommand>)
          return typed.target.entityId;
        else if constexpr (std::is_same_v<Command, SetValuesCommand>)
          return typed.values.empty() ? std::string{}
                                      : typed.values.front().target.entityId;
        else if constexpr (std::is_same_v<Command, InsertEntityCommand>)
          return typed.entity.value("id", std::string{});
        else if constexpr (std::is_same_v<Command, RemoveEntitiesCommand>)
          return typed.entities.empty()
                     ? std::string{}
                     : typed.entities.front().entity.value("id", std::string{});
        else if constexpr (std::is_same_v<Command, DuplicateEntityCommand>)
          return typed.entities.empty()
                     ? std::string{}
                     : typed.entities.front().value("id", std::string{});
        else
          return typed.entityId;
      },
      command);
}

} // namespace demi::editor
