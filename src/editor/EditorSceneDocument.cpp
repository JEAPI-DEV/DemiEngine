#include "editor/EditorSceneDocument.h"

#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/schema/Validation.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace demi::editor {
namespace {

std::string validationMessage(
    const std::vector<runtime::scene_loading::ComponentValidationError>
        &errors) {
  if (errors.empty())
    return {};
  const auto &first = errors.front();
  return first.field.empty() ? first.message
                             : first.field + " " + first.message;
}

bool stagedHasErrors(const std::filesystem::path &path,
                     const nlohmann::json &document, std::string &error) {
  const Diagnostics diagnostics = demi::validateSceneDocument(path, document);
  for (const Diagnostic &diagnostic : diagnostics) {
    if (diagnostic.severity == Severity::Error) {
      error = diagnostic.code + ": " + diagnostic.message;
      return true;
    }
  }
  return false;
}

} // namespace

bool EditorSceneDocument::open(const std::filesystem::path &path,
                               std::string &error) {
  std::string text;
  FileRevision revision;
  if (!store_.read(path, text, revision, error))
    return false;
  try {
    nlohmann::json parsed = nlohmann::json::parse(text);
    if (!parsed.is_object() || !parsed.contains("format_version") ||
        !parsed.contains("entities") || !parsed["entities"].is_array()) {
      error = "The active scene is not an editable scene document.";
      return false;
    }
    path_ = path;
    revision_ = revision;
    document_ = std::move(parsed);
    savedCanonical_ = document_.dump();
    undo_.clear();
    redo_.clear();
    continuousTarget_.reset();
    lastChangedEntityId_.clear();
    return true;
  } catch (const nlohmann::json::exception &exception) {
    error =
        "Could not parse the active scene: " + std::string(exception.what());
    return false;
  }
}

bool EditorSceneDocument::reload(std::string &error) {
  if (isDirty()) {
    error =
        "The scene has unsaved changes. Save or undo them before refreshing.";
    return false;
  }
  return open(path_, error);
}

bool EditorSceneDocument::save(std::string &error) {
  if (!isDirty())
    return true;
  const std::string serialized = document_.dump(2) + '\n';
  FileRevision revision;
  if (!store_.writeIfUnchanged(path_, serialized, revision_, revision, error))
    return false;
  revision_ = revision;
  savedCanonical_ = document_.dump();
  return true;
}

bool EditorSceneDocument::stageAndCommit(SceneCommand command,
                                         std::string &error) {
  nlohmann::json staged = document_;
  applySceneCommand(staged, command, true);
  if (stagedHasErrors(path_, staged, error))
    return false;
  applySceneCommand(document_, command, true);
  lastChangedEntityId_ = sceneCommandEntityId(command);
  undo_.push_back(std::move(command));
  redo_.clear();
  continuousTarget_.reset();
  return true;
}

bool EditorSceneDocument::setValue(SceneValueTarget target,
                                   nlohmann::json replacement,
                                   const bool continuous, std::string &error) {
  nlohmann::json *current = value(target);
  if (current == nullptr) {
    error = "The selected authored field no longer exists.";
    return false;
  }
  if (*current == replacement)
    return true;
  if (!validate(target, replacement, error))
    return false;
  nlohmann::json staged = document_;
  nlohmann::json *stagedValue = valueInDocument(staged, target);
  if (stagedValue == nullptr) {
    error = "The selected authored field no longer exists.";
    return false;
  }
  *stagedValue = replacement;
  if (stagedHasErrors(path_, staged, error))
    return false;

  if (continuous && continuousTarget_ == target && !undo_.empty() &&
      std::holds_alternative<SetValueCommand>(undo_.back())) {
    auto &command = std::get<SetValueCommand>(undo_.back());
    command.after = std::move(replacement);
    *current = command.after;
  } else {
    SetValueCommand command{.target = std::move(target),
                            .before = *current,
                            .after = std::move(replacement)};
    *current = command.after;
    undo_.push_back(std::move(command));
  }
  redo_.clear();
  const SetValueCommand &top = std::get<SetValueCommand>(undo_.back());
  lastChangedEntityId_ = top.target.entityId;
  continuousTarget_ = continuous ? std::optional(top.target) : std::nullopt;
  return true;
}

bool EditorSceneDocument::createEntity(std::string &error) {
  nlohmann::json *entities = entitiesArray(document_);
  if (entities == nullptr) {
    error = "The scene has no entities array.";
    return false;
  }
  const std::string id = uniqueEntityId(document_, "ent_new");
  nlohmann::json entity{{"id", id},
                        {"name", "New Entity"},
                        {"components", nlohmann::json::object()}};
  return stageAndCommit(InsertEntityCommand{.index = entities->size(),
                                            .entity = std::move(entity)},
                        error);
}

bool EditorSceneDocument::deleteEntity(const std::string_view id,
                                       std::string &error) {
  if (entity(id) == nullptr) {
    error = "The entity no longer exists.";
    return false;
  }
  std::vector<IndexedSceneEntity> removed;
  for (const std::string &member : collectSubtreeIds(document_, id)) {
    const std::optional<std::size_t> index = entityIndex(document_, member);
    const nlohmann::json *authored = entity(member);
    if (!index.has_value() || authored == nullptr) {
      error = "The entity subtree changed while preparing deletion.";
      return false;
    }
    removed.push_back({.index = *index, .entity = *authored});
  }
  std::ranges::sort(removed, {}, &IndexedSceneEntity::index);
  return stageAndCommit(RemoveEntitiesCommand{.entities = std::move(removed)},
                        error);
}

bool EditorSceneDocument::reparent(const std::string_view id,
                                   std::optional<std::string> newParent,
                                   std::string &error) {
  nlohmann::json *authored = findEntity(document_, id);
  if (authored == nullptr) {
    error = "The entity no longer exists.";
    return false;
  }
  const char *transform = transformComponentName(*authored);
  if (transform == nullptr) {
    error = "Reparenting requires a Transform3D or Transform2D component on "
            "the entity.";
    return false;
  }
  nlohmann::json *component = findComponent(*authored, transform);
  std::optional<std::string> before;
  if (const auto parent = component->find("parent");
      parent != component->end() && parent->is_string())
    before = parent->get<std::string>();

  return stageAndCommit(ReparentCommand{.entityId = std::string(id),
                                        .component = transform,
                                        .before = std::move(before),
                                        .after = std::move(newParent)},
                        error);
}

bool EditorSceneDocument::duplicateEntity(const std::string_view id,
                                          std::string &error) {
  const nlohmann::json *source = entity(id);
  if (source == nullptr) {
    error = "The entity no longer exists.";
    return false;
  }
  const std::vector<std::string> subtree = collectSubtreeIds(document_, id);
  std::unordered_map<std::string, std::string> remap;
  std::unordered_set<std::string> reserved;
  for (const std::string &member : subtree) {
    remap[member] = uniqueEntityId(document_, member + "_copy", reserved);
    reserved.insert(remap[member]);
  }

  std::vector<nlohmann::json> copies;
  copies.reserve(subtree.size());
  for (const std::string &member : subtree) {
    const nlohmann::json *original = entity(member);
    if (original == nullptr)
      continue;
    nlohmann::json copy = *original;
    copy["id"] = remap[member];
    remapParentReferences(copy, remap);
    copies.push_back(std::move(copy));
  }
  if (copies.empty()) {
    error = "The entity could not be duplicated.";
    return false;
  }

  nlohmann::json *entities = entitiesArray(document_);
  return stageAndCommit(DuplicateEntityCommand{.index = entities == nullptr
                                                            ? 0
                                                            : entities->size(),
                                               .entities = std::move(copies)},
                        error);
}

bool EditorSceneDocument::addComponent(const std::string_view id,
                                       const std::string_view componentName,
                                       std::string &error) {
  if (entity(id) == nullptr) {
    error = "The entity no longer exists.";
    return false;
  }
  if (component(id, componentName) != nullptr) {
    error = "The entity already has a " + std::string(componentName) +
            " component.";
    return false;
  }
  const runtime::scene_loading::ComponentDescriptor *descriptor =
      runtime::scene_loading::findComponentDescriptor(componentName);
  if (descriptor == nullptr) {
    error = "Unknown component: " + std::string(componentName);
    return false;
  }
  return stageAndCommit(
      AddComponentCommand{
          .entityId = std::string(id),
          .componentName = std::string(componentName),
          .component = runtime::scene_loading::componentDefaults(*descriptor)},
      error);
}

bool EditorSceneDocument::removeComponent(const std::string_view id,
                                          const std::string_view componentName,
                                          std::string &error) {
  const nlohmann::json *authored = component(id, componentName);
  if (authored == nullptr) {
    error = "The component no longer exists.";
    return false;
  }
  return stageAndCommit(
      RemoveComponentCommand{.entityId = std::string(id),
                             .componentName = std::string(componentName),
                             .component = *authored},
      error);
}

bool EditorSceneDocument::undo(std::string &error) {
  if (undo_.empty()) {
    error = "There is nothing to undo.";
    return false;
  }
  SceneCommand command = std::move(undo_.back());
  undo_.pop_back();
  applySceneCommand(document_, command, false);
  lastChangedEntityId_ = sceneCommandEntityId(command);
  redo_.push_back(std::move(command));
  continuousTarget_.reset();
  return true;
}

bool EditorSceneDocument::redo(std::string &error) {
  if (redo_.empty()) {
    error = "There is nothing to redo.";
    return false;
  }
  SceneCommand command = std::move(redo_.back());
  redo_.pop_back();
  applySceneCommand(document_, command, true);
  lastChangedEntityId_ = sceneCommandEntityId(command);
  undo_.push_back(std::move(command));
  continuousTarget_.reset();
  return true;
}

bool EditorSceneDocument::isDirty() const {
  return !path_.empty() && document_.dump() != savedCanonical_;
}

const nlohmann::json *
EditorSceneDocument::entity(const std::string_view id) const {
  return findEntity(document_, id);
}

const nlohmann::json *
EditorSceneDocument::component(const std::string_view entityId,
                               const std::string_view name) const {
  const nlohmann::json *authoredEntity = entity(entityId);
  return authoredEntity == nullptr ? nullptr
                                   : findComponent(*authoredEntity, name);
}

nlohmann::json *EditorSceneDocument::value(const SceneValueTarget &target) {
  return valueInDocument(document_, target);
}

const nlohmann::json *
EditorSceneDocument::value(const SceneValueTarget &target) const {
  return valueInDocument(document_, target);
}

bool EditorSceneDocument::validate(const SceneValueTarget &target,
                                   const nlohmann::json &replacement,
                                   std::string &error) const {
  if (target.component.empty()) {
    if (target.field == "name" && !replacement.is_string())
      error = "Entity names must be strings.";
    else if (target.field == "enabled" && !replacement.is_boolean())
      error = "Entity enabled state must be a boolean.";
    else if (target.field == "layer" && !replacement.is_string())
      error = "Entity layers must be strings.";
    else
      return true;
    return false;
  }

  const auto *descriptor =
      runtime::scene_loading::findComponentDescriptor(target.component);
  const nlohmann::json *authoredComponent =
      component(target.entityId, target.component);
  if (descriptor == nullptr || authoredComponent == nullptr)
    return true;
  nlohmann::json staged = *authoredComponent;
  staged[target.field] = replacement;
  const auto errors =
      runtime::scene_loading::validateComponent(*descriptor, staged);
  if (errors.empty())
    return true;
  error = validationMessage(errors);
  return false;
}

} // namespace demi::editor
