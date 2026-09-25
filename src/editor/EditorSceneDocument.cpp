#include "editor/EditorSceneDocument.h"

#include "demi/filesystem/ProjectPaths.h"
#include "editor/EditorAuthoredJson.h"
#include "editor/EditorEntityHierarchy.h"
#include "demi/runtime/scene/composition/EntityHierarchy.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "editor/EditorSpecializedDocument.h"

#include "demi/diagnostics/Diagnostic.h"
#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/EntityPresets.h"
#include "demi/schema/Validation.h"

#include <algorithm>
#include <array>
#include <string>
#include <tuple>
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
  const Diagnostics diagnostics =
      isPrefabFile(path) ? validateSpecializedDocument(
                               EditorSpecializedKind::Prefab, path, document)
                         : demi::validateSceneDocument(path, document);
  for (const Diagnostic &diagnostic : diagnostics) {
    if (diagnostic.severity == Severity::Error) {
      error = diagnostic.code + ": " + diagnostic.message;
      return true;
    }
  }
  if (runtime::composition::hasPrefabComposition(document)) {
    const auto expansion = runtime::composition::expandScene(path, document, false);
    if (!expansion.document) {
      error = expansion.diagnostics.empty() ? "Prefab composition failed."
                                            : expansion.diagnostics.front().message;
      return true;
    }
  }
  return false;
}

SceneValueTarget issueTarget(const SceneCommand &command) {
  if (const auto *setValue = std::get_if<SetValueCommand>(&command))
    return setValue->target;
  if (const auto *setValues = std::get_if<SetValuesCommand>(&command);
      setValues != nullptr && !setValues->values.empty())
    return setValues->values.front().target;
  return {.entityId = sceneCommandEntityId(command)};
}

} // namespace

bool EditorSceneDocument::open(const std::filesystem::path &path,
                               std::string &error) {
  std::error_code filesystemError;
  const std::filesystem::path resolvedPath =
      std::filesystem::absolute(path, filesystemError).lexically_normal();
  if (filesystemError) {
    error = "Could not resolve the scene path: " + filesystemError.message();
    return false;
  }
  std::string text;
  FileRevision revision;
  if (!store_.read(resolvedPath, text, revision, error))
    return false;
  try {
    nlohmann::json parsed = nlohmann::json::parse(text);
    if (isPrefabFile(resolvedPath) &&
        stagedHasErrors(resolvedPath, parsed, error))
      return false;
    if (!parsed.is_object() || !parsed.contains("format_version") ||
        !parsed.contains("entities") || !parsed["entities"].is_array()) {
      error = "The active scene is not an editable scene document.";
      return false;
    }
    path_ = resolvedPath;
    revision_ = revision;
    document_ = std::move(parsed);
    savedDocument_ = document_;
    originalText_ = std::move(text);
    savedCanonical_ = document_.dump();
    undo_.clear();
    redo_.clear();
    continuousTarget_.reset();
    continuousRedoBackup_.clear();
    lastChangedEntityId_.clear();
    issue_.reset();
    hasExternalConflict_ = false;
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
  if (!isDirty()) {
    hasExternalConflict_ = false;
    return true;
  }
  const std::string serialized = serializedText();
  FileRevision revision;
  const DocumentWriteStatus status =
      store_.writeIfUnchanged(path_, serialized, revision_, revision, error);
  if (status == DocumentWriteStatus::Conflict) {
    hasExternalConflict_ = true;
    return false;
  }
  if (status == DocumentWriteStatus::Failed)
    return false;
  revision_ = revision;
  savedCanonical_ = document_.dump();
  savedDocument_ = document_;
  originalText_ = serialized;
  hasExternalConflict_ = false;
  return true;
}

std::string EditorSceneDocument::serializedText() const {
  if (const auto patched =
          patchEditorJsonSource(originalText_, savedDocument_, document_))
    return *patched;
  return document_.dump(2) + '\n';
}

bool EditorSceneDocument::restore(nlohmann::json document, std::string &error) {
  if (stagedHasErrors(path_, document, error))
    return false;
  document_ = std::move(document);
  undo_.clear();
  redo_.clear();
  continuousTarget_.reset();
  continuousRedoBackup_.clear();
  issue_.reset();
  lastChangedEntityId_.clear();
  return true;
}

bool EditorSceneDocument::setHud(std::optional<nlohmann::json> value, std::string &error) {
  const auto before = document_.contains("hud")
      ? std::optional<nlohmann::json>(document_["hud"]) : std::nullopt;
  if (before == value) return true;
  return stageAndCommit(SetSceneHudCommand{before, std::move(value)}, error);
}

bool EditorSceneDocument::resolveExternalChange(
    const ExternalChangeDecision decision,
    const std::filesystem::path &copyPath, std::string &error) {
  if (!hasExternalConflict_) {
    error = "There is no external scene change to resolve.";
    return false;
  }
  switch (decision) {
  case ExternalChangeDecision::ReloadFromDisk:
    return open(path_, error);
  case ExternalChangeDecision::KeepEditing:
  case ExternalChangeDecision::Cancel:
    hasExternalConflict_ = false;
    return true;
  case ExternalChangeDecision::SaveCopy: {
    if (copyPath.empty()) {
      error = "Choose a path for the scene copy.";
      return false;
    }
    std::error_code filesystemError;
    const std::filesystem::path destination =
        std::filesystem::absolute(copyPath, filesystemError).lexically_normal();
    if (filesystemError) {
      error = "Could not resolve the copy destination: " +
              filesystemError.message();
      return false;
    }
    if (destination == path_.lexically_normal()) {
      error = "The copy must use a different path from the external scene.";
      return false;
    }
    if (!store_.writeNew(destination, serializedText(), error))
      return false;
    hasExternalConflict_ = false;
    return true;
  }
  }
  error = "Unknown external-change decision.";
  return false;
}

void EditorSceneDocument::reject(SceneValueTarget target,
                                 const std::string &error) {
  issue_ = EditorDocumentIssue{.target = std::move(target), .message = error};
}

bool EditorSceneDocument::stageAndCommit(SceneCommand command,
                                         std::string &error) {
  nlohmann::json staged = document_;
  applySceneCommand(staged, command, true);
  if (stagedHasErrors(path_, staged, error)) {
    reject(issueTarget(command), error);
    return false;
  }
  applySceneCommand(document_, command, true);
  lastChangedEntityId_ = sceneCommandEntityId(command);
  undo_.push_back(std::move(command));
  redo_.clear();
  endContinuousEdit();
  clearIssue();
  return true;
}

bool EditorSceneDocument::setValue(SceneValueTarget target,
                                   nlohmann::json replacement,
                                   const bool continuous, std::string &error) {
  if (!target.isPrefabOverride() && target.field == "parent" &&
      (target.component == "Transform3D" || target.component == "Transform2D" ||
       target.component == "IsoTransform") && replacement.is_string())
    return reparent(target.entityId, replacement.get<std::string>(), error);
  const nlohmann::json *current = value(target);
  replacement = normalizeEditorAuthoredValue(std::move(replacement), current);
  if (current != nullptr && *current == replacement) {
    clearIssue();
    return true;
  }
  if (!validate(target, replacement, error)) {
    reject(target, error);
    return false;
  }

  SetValueCommand next{.target = target,
                       .before = current == nullptr
                                     ? std::nullopt
                                     : std::optional<nlohmann::json>(*current),
                       .after = std::move(replacement)};
  if (target.isPrefabOverride()) {
    const auto *instance =
        findPrefabInstance(document_, target.prefabInstanceId);
    if (instance && instance->contains("overrides"))
      next.prefabOverridesBefore = instance->at("overrides");
  }
  if (!target.isPrefabOverride() && !target.component.empty()) {
    const auto *owner = entity(target.entityId);
    next.createdComponent = owner && component(target.entityId, target.component) == nullptr;
    next.createdComponentsContainer = next.createdComponent && !owner->contains("components");
  }
  nlohmann::json staged = document_;
  if (!assignValueInDocument(staged, target, next.after)) {
    error = "The selected entity or component no longer exists.";
    reject(target, error);
    return false;
  }
  if (stagedHasErrors(path_, staged, error)) {
    reject(target, error);
    return false;
  }

  const bool extendsContinuous =
      continuous && continuousTarget_ == target && !undo_.empty() &&
      std::holds_alternative<SetValueCommand>(undo_.back());
  if (extendsContinuous) {
    auto &command = std::get<SetValueCommand>(undo_.back());
    command.after = std::move(next.after);
    (void)assignValueInDocument(document_, target, command.after);
  } else {
    if (continuous)
      continuousRedoBackup_ = redo_;
    else
      continuousRedoBackup_.clear();
    (void)assignValueInDocument(document_, target, next.after);
    undo_.push_back(std::move(next));
  }
  redo_.clear();
  const SetValueCommand &top = std::get<SetValueCommand>(undo_.back());
  lastChangedEntityId_ = top.target.entityId;
  continuousTarget_ = continuous ? std::optional(top.target) : std::nullopt;
  if (!continuous)
    continuousRedoBackup_.clear();
  clearIssue();
  return true;
}

bool EditorSceneDocument::setValues(std::vector<SceneValueTarget> targets,
                                    nlohmann::json replacement,
                                    std::string &error) {
  if (targets.empty()) {
    error = "A multi-edit requires at least one target.";
    return false;
  }
  std::ranges::sort(
      targets, [](const SceneValueTarget &left, const SceneValueTarget &right) {
        return std::tie(left.entityId, left.component, left.field,
                        left.prefabInstanceId, left.prefabEntityId) <
               std::tie(right.entityId, right.component, right.field,
                        right.prefabInstanceId, right.prefabEntityId);
      });
  if (std::ranges::adjacent_find(targets) != targets.end()) {
    error = "A multi-edit cannot contain the same field twice.";
    reject(targets.front(), error);
    return false;
  }

  SetValuesCommand command;
  command.values.reserve(targets.size());
  for (const SceneValueTarget &target : targets) {
    const nlohmann::json *current = value(target);
    if (current == nullptr) {
      error = "Every multi-edit target must be an authored field.";
      reject(target, error);
      return false;
    }
    nlohmann::json normalized =
        normalizeEditorAuthoredValue(replacement, current);
    if (!validate(target, normalized, error)) {
      reject(target, error);
      return false;
    }
    command.values.push_back(
        {.target = target, .before = *current, .after = std::move(normalized)});
    if (target.isPrefabOverride()) {
      const auto *instance =
          findPrefabInstance(document_, target.prefabInstanceId);
      if (instance && instance->contains("overrides"))
        command.values.back().prefabOverridesBefore = instance->at("overrides");
    }
  }
  return stageAndCommit(std::move(command), error);
}

void EditorSceneDocument::endContinuousEdit() {
  continuousTarget_.reset();
  continuousRedoBackup_.clear();
}

bool EditorSceneDocument::cancelContinuousEdit(std::string &error) {
  if (!continuousTarget_)
    return true;
  if (undo_.empty() || !std::holds_alternative<SetValueCommand>(undo_.back()) ||
      std::get<SetValueCommand>(undo_.back()).target != *continuousTarget_) {
    error = "The active continuous edit no longer matches command history.";
    endContinuousEdit();
    return false;
  }
  SceneCommand command = std::move(undo_.back());
  undo_.pop_back();
  applySceneCommand(document_, command, false);
  lastChangedEntityId_ = sceneCommandEntityId(command);
  redo_ = std::move(continuousRedoBackup_);
  continuousTarget_.reset();
  clearIssue();
  return true;
}

bool EditorSceneDocument::removeValue(SceneValueTarget target,
                                      std::string &error) {
  if (!target.isPrefabOverride() && target.field == "parent" &&
      (target.component == "Transform3D" || target.component == "Transform2D" ||
       target.component == "IsoTransform"))
    return reparent(target.entityId, std::nullopt, error);
  const nlohmann::json *current = value(target);
  if (current == nullptr) {
    error = "The selected authored field no longer exists.";
    reject(target, error);
    return false;
  }
  SetValueCommand command{
      .target = target, .before = *current, .after = std::nullopt};
  if (target.isPrefabOverride()) {
    const auto *instance =
        findPrefabInstance(document_, target.prefabInstanceId);
    if (instance && instance->contains("overrides"))
      command.prefabOverridesBefore = instance->at("overrides");
    if (!target.component.empty()) {
      // Resetting the last property of a locally added component must not
      // remove the component. Resolve inheritance only for this user action.
      const auto inherited = prefabInheritsComponent(target, error);
      if (!inherited)
        return false;
      command.preserveEmptyPrefabComponent = !*inherited;
    }
  }
  return stageAndCommit(std::move(command), error);
}

bool EditorSceneDocument::createEntity(std::string &error,
                                       std::optional<std::string> parent) {
  nlohmann::json *entities = entitiesArray(document_);
  if (entities == nullptr) {
    error = "The scene has no entities array.";
    reject({}, error);
    return false;
  }
  const std::string id = uniqueEntityId(document_, "ent_new");
  nlohmann::json entity{{"id", id},
                        {"name", "New Entity"},
                        {"components", nlohmann::json::object()}};
  if (parent.has_value()) {
    const auto flat = runtime::composition::flattenEntityHierarchy(*entities);
    const nlohmann::json *parentEntity = runtime::composition::findAuthoredEntity(flat, *parent);
    if (parentEntity == nullptr) {
      error = "The parent entity no longer exists.";
      reject({.entityId = *parent}, error);
      return false;
    }
    const char *transform = transformComponentName(*parentEntity);
    if (transform == nullptr) {
      error = "Creating a child requires a Transform2D, Transform3D, or "
              "IsoTransform on the parent.";
      reject({.entityId = *parent}, error);
      return false;
    }
    entity["components"][transform] = nlohmann::json::object();
  }
  if (parent) {
    auto staged = document_;
    insertEntityUnder(staged, std::move(entity), *parent);
    return stageAndCommit(EntityHierarchyCommand{.entityId = id,
        .before = document_["entities"], .after = std::move(staged["entities"])}, error);
  }
  return stageAndCommit(InsertEntityCommand{.index = entities->size(),
                                            .entity = std::move(entity)},
                        error);
}

bool EditorSceneDocument::instantiatePrefab(const std::string_view reference,
                                            std::string &error) {
  constexpr std::string_view Prefix = "prefab://";
  if (!reference.starts_with(Prefix) || reference.size() == Prefix.size()) {
    error = "Prefab instances require a prefab:// reference.";
    reject({}, error);
    return false;
  }
  nlohmann::json *entities = entitiesArray(document_);
  if (entities == nullptr) {
    error = "The scene has no entities array.";
    reject({}, error);
    return false;
  }

  std::string_view base = reference.substr(Prefix.size());
  if (const std::size_t separator = base.rfind('/');
      separator != std::string_view::npos)
    base.remove_prefix(separator + 1);
  if (base.empty())
    base = "prefab_instance";
  const std::string id = uniqueEntityId(document_, base);
  return stageAndCommit(
      InsertEntityCommand{
          .index = entities->size(),
          .entity = {{"id", id}, {"prefab", std::string(reference)}}},
      error);
}

bool EditorSceneDocument::createPresetEntity(std::string_view preset,
                                             std::string &error) {
  const auto known = runtime::scene_loading::knownEntityPresets();
  if (std::ranges::find(known, preset) == known.end()) {
    error = "Unknown entity preset.";
    return false;
  }
  nlohmann::json *entities = entitiesArray(document_);
  if (entities == nullptr) {
    error = "The scene has no entities array.";
    reject({}, error);
    return false;
  }
  const std::string id = uniqueEntityId(document_, "ent_new");
  nlohmann::json entity{{"id", id},
                        {"name", std::string(preset) + " Entity"},
                        {"preset", std::string(preset)},
                        {"components", nlohmann::json::object()}};
  // Physics presets describe bodies/colliders, so new editor objects also need
  // an authored pose for gizmos and hierarchy operations. The 2D preset supplies it.
  if (preset != "prop_2d")
    entity["components"]["Transform3D"] = nlohmann::json::object();
  return stageAndCommit(InsertEntityCommand{.index = entities->size(),
                                            .entity = std::move(entity)},
                        error);
}

bool EditorSceneDocument::deleteEntity(const std::string_view id,
                                       std::string &error) {
  const std::array ids{std::string(id)};
  return deleteEntities(ids, error);
}

bool EditorSceneDocument::unpackPreset(const std::string_view id, std::string &error) {
  const auto *source = entity(id);
  if (!source || !source->contains("preset")) {
    error = "Select an authored preset entity.";
    return false;
  }
  auto staged = document_;
  auto *replacement = findEntity(staged, id);
  *replacement = runtime::scene_loading::expandEntityPreset(*source);
  replacement->erase("size"); // The preset has applied this shorthand to its components.
  return stageAndCommit(EntityHierarchyCommand{.entityId = std::string(id),
      .before = document_["entities"], .after = staged["entities"]}, error);
}

bool EditorSceneDocument::deleteEntities(const std::span<const std::string> ids,
                                         std::string &error) {
  if (ids.empty()) {
    error = "Select at least one entity to delete.";
    return false;
  }
  std::unordered_set<std::string> members;
  std::unordered_set<std::string> instancesToRemove;
  const auto instances = document_.find("instances");
  for (const std::string &id : ids) {
    if (entity(id) == nullptr) {
      if (instances != document_.end() && instances->is_array() &&
          std::ranges::any_of(*instances, [&](const auto &item) {
            return item.value("id", std::string{}) == id;
          })) {
        instancesToRemove.insert(id);
        continue;
      }
      error = "The entity no longer exists.";
      reject({.entityId = id}, error);
      return false;
    }
    for (std::string member : collectSubtreeIds(document_, id))
      members.insert(std::move(member));
  }
  auto staged = document_["entities"];
  eraseEntitySubtrees(staged, members);
  EntityHierarchyCommand command{.entityId = ids.front(),
      .before = document_["entities"], .after = std::move(staged)};
  if (!instancesToRemove.empty()) {
    command.instancesBefore = *instances;
    auto remaining = nlohmann::json::array();
    for (const auto &instance : *instances)
      if (!instancesToRemove.contains(instance.value("id", std::string{})))
        remaining.push_back(instance);
    command.instancesAfter = std::move(remaining);
  }
  return stageAndCommit(std::move(command), error);
}

bool EditorSceneDocument::reparent(const std::string_view id,
                                   std::optional<std::string> newParent,
                                   std::string &error) {
  nlohmann::json *authored = findEntity(document_, id);
  if (authored == nullptr) {
    error = "The entity no longer exists.";
    reject({.entityId = std::string(id)}, error);
    return false;
  }
  const auto flat = runtime::composition::flattenEntityHierarchy(document_["entities"]);
  const auto *effective = runtime::composition::findAuthoredEntity(flat, id);
  const char *transform = transformComponentName(*effective);
  if (transform == nullptr) {
    error = "Reparenting requires a Transform3D, Transform2D, or IsoTransform "
            "component on the entity.";
    reject({.entityId = std::string(id)}, error);
    return false;
  }
  const auto subtree = collectSubtreeIds(document_, id);
  if (newParent && std::ranges::find(subtree, *newParent) != subtree.end()) {
    error = "An entity cannot be parented to itself or its descendants.";
    reject({.entityId = std::string(id)}, error);
    return false;
  }
  auto moved = *authored;
  if (!findComponent(moved, transform))
    moved["components"][transform] = nlohmann::json::object();
  auto staged = document_;
  eraseEntitySubtrees(staged["entities"], {std::string(id)});
  insertEntityUnder(staged, std::move(moved), newParent.value_or(""));
  return stageAndCommit(EntityHierarchyCommand{.entityId = std::string(id),
      .before = document_["entities"], .after = std::move(staged["entities"])}, error);
}

bool EditorSceneDocument::duplicateEntity(const std::string_view id,
                                          std::string &error) {
  const nlohmann::json *source = entity(id);
  if (source == nullptr) {
    const auto instances = document_.find("instances");
    if (instances != document_.end() && instances->is_array()) {
      const auto instance = std::ranges::find_if(*instances, [&](const auto &item) {
        return item.value("id", std::string{}) == id;
      });
      if (instance != instances->end()) {
        auto after = *instances;
        auto copy = *instance;
        const auto copyId = uniqueEntityId(document_, std::string(id) + "_copy");
        copy["id"] = copyId;
        after.push_back(std::move(copy));
        return stageAndCommit(EntityHierarchyCommand{
            .entityId = copyId, .before = document_["entities"], .after = document_["entities"],
            .instancesBefore = *instances, .instancesAfter = std::move(after)}, error);
      }
    }
    error = "The entity no longer exists.";
    reject({.entityId = std::string(id)}, error);
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
  const auto flat = runtime::composition::flattenEntityHierarchy(document_["entities"]);
  for (const std::string &member : subtree) {
    const nlohmann::json *original = runtime::composition::findAuthoredEntity(flat, member);
    if (original == nullptr)
      continue;
    nlohmann::json copy = *original;
    copy["id"] = remap[member];
    remapParentReferences(copy, remap);
    copies.push_back(std::move(copy));
  }
  if (copies.empty()) {
    error = "The entity could not be duplicated.";
    reject({.entityId = std::string(id)}, error);
    return false;
  }

  auto staged = document_;
  for (auto &copy : copies) {
    const auto parent = transformParentId(copy);
    insertEntityUnder(staged, std::move(copy), parent);
  }
  return stageAndCommit(EntityHierarchyCommand{.entityId = remap.at(std::string(id)),
      .before = document_["entities"], .after = std::move(staged["entities"])}, error);
}

bool EditorSceneDocument::addComponent(const std::string_view id,
                                       const std::string_view componentName,
                                       std::string &error) {
  const runtime::scene_loading::ComponentDescriptor *descriptor =
      runtime::scene_loading::findComponentDescriptor(componentName);
  if (descriptor == nullptr) {
    error = "Unknown component: " + std::string(componentName);
    reject(
        {.entityId = std::string(id), .component = std::string(componentName)},
        error);
    return false;
  }
  auto authoredDefaults = runtime::scene_loading::componentDefaults(*descriptor);
  for (const auto &field : descriptor->fields)
    if (!field.required)
      authoredDefaults.erase(std::string(field.name));
  return addComponent(id, componentName, std::move(authoredDefaults), error);
}

bool EditorSceneDocument::addComponent(const std::string_view id,
                                       const std::string_view componentName,
                                       nlohmann::json initialValues,
                                       std::string &error) {
  if (entity(id) == nullptr) {
    error = "The entity no longer exists.";
    reject(
        {.entityId = std::string(id), .component = std::string(componentName)},
        error);
    return false;
  }
  if (component(id, componentName) != nullptr) {
    error = "The entity already has a " + std::string(componentName) +
            " component.";
    reject(
        {.entityId = std::string(id), .component = std::string(componentName)},
        error);
    return false;
  }
  const runtime::scene_loading::ComponentDescriptor *descriptor =
      runtime::scene_loading::findComponentDescriptor(componentName);
  if (descriptor == nullptr) {
    error = "Unknown component: " + std::string(componentName);
    reject(
        {.entityId = std::string(id), .component = std::string(componentName)},
        error);
    return false;
  }
  if (!initialValues.is_object()) {
    error = "Initial component values must be an object.";
    reject(
        {.entityId = std::string(id), .component = std::string(componentName)},
        error);
    return false;
  }
  return stageAndCommit(
      AddComponentCommand{
          .entityId = std::string(id),
          .componentName = std::string(componentName),
          .component = std::move(initialValues)},
      error);
}

bool EditorSceneDocument::addScriptComponent(const std::string_view id,
                                             std::string module,
                                             nlohmann::json properties,
                                             std::string &error) {
  if (entity(id) == nullptr) {
    error = "The entity no longer exists.";
    reject({.entityId = std::string(id), .component = "LuaScript"}, error);
    return false;
  }
  if (component(id, "LuaScript") != nullptr) {
    error = "This entity already has a Lua script component.";
    reject({.entityId = std::string(id), .component = "LuaScript"}, error);
    return false;
  }
  if (module.empty() || !module.starts_with("script://") ||
      !properties.is_object()) {
    error =
        "Script components require a script:// module and object properties.";
    reject({.entityId = std::string(id), .component = "LuaScript"}, error);
    return false;
  }
  return stageAndCommit(
      AddComponentCommand{.entityId = std::string(id),
                          .componentName = "LuaScript",
                          .component = {{"module", std::move(module)},
                                        {"properties", std::move(properties)}}},
      error);
}

bool EditorSceneDocument::removeComponent(const std::string_view id,
                                          const std::string_view componentName,
                                          std::string &error) {
  const nlohmann::json *authored = component(id, componentName);
  if (authored == nullptr) {
    error = "The component no longer exists.";
    reject(
        {.entityId = std::string(id), .component = std::string(componentName)},
        error);
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
  endContinuousEdit();
  clearIssue();
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
  endContinuousEdit();
  clearIssue();
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

const std::string *
EditorSceneDocument::issueFor(const SceneValueTarget &target) const {
  return issue_.has_value() && issue_->target.entityId == target.entityId &&
                 issue_->target.component == target.component &&
                 issue_->target.field == target.field
             ? &issue_->message
             : nullptr;
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
