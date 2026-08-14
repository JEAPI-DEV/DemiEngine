#include "editor/EditorSceneDocument.h"

#include "demi/runtime/scene/ComponentRegistry.h"

#include <algorithm>

namespace demi::editor {
namespace {

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

template <typename Json>
Json *findComponent(Json &entity, const std::string_view name) {
  auto components = entity.find("components");
  Json &source = components != entity.end() && components->is_object()
                     ? *components
                     : entity;
  auto component = source.find(name);
  return component == source.end() || !component->is_object() ? nullptr
                                                              : &*component;
}

std::string validationMessage(
    const std::vector<runtime::scene_loading::ComponentValidationError>
        &errors) {
  if (errors.empty())
    return {};
  const auto &first = errors.front();
  return first.field.empty() ? first.message
                             : first.field + " " + first.message;
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

  if (continuous && continuousTarget_ == target && !undo_.empty()) {
    undo_.back().after = replacement;
    *current = std::move(replacement);
  } else {
    SetValueCommand command{.target = std::move(target),
                            .before = *current,
                            .after = std::move(replacement)};
    *current = command.after;
    undo_.push_back(std::move(command));
  }
  redo_.clear();
  continuousTarget_ =
      continuous ? std::optional(undo_.back().target) : std::nullopt;
  lastChangedEntityId_ = undo_.back().target.entityId;
  return true;
}

bool EditorSceneDocument::undo(std::string &error) {
  if (undo_.empty()) {
    error = "There is nothing to undo.";
    return false;
  }
  SetValueCommand command = std::move(undo_.back());
  undo_.pop_back();
  apply(command, false);
  lastChangedEntityId_ = command.target.entityId;
  redo_.push_back(std::move(command));
  continuousTarget_.reset();
  return true;
}

bool EditorSceneDocument::redo(std::string &error) {
  if (redo_.empty()) {
    error = "There is nothing to redo.";
    return false;
  }
  SetValueCommand command = std::move(redo_.back());
  redo_.pop_back();
  apply(command, true);
  lastChangedEntityId_ = command.target.entityId;
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
  nlohmann::json *authoredEntity = findEntity(document_, target.entityId);
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

const nlohmann::json *
EditorSceneDocument::value(const SceneValueTarget &target) const {
  const nlohmann::json *authoredEntity = findEntity(document_, target.entityId);
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

void EditorSceneDocument::apply(const SetValueCommand &command,
                                const bool forward) {
  if (nlohmann::json *target = value(command.target))
    *target = forward ? command.after : command.before;
}

} // namespace demi::editor
