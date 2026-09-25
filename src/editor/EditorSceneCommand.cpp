#include "editor/EditorSceneCommand.h"

#include <type_traits>

namespace demi::editor {
namespace {
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
        else if constexpr (std::is_same_v<Command, SetValueCommand>)
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
