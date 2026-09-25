#include "editor/EditorSceneDocument.h"

#include "demi/runtime/scene/ComponentRegistry.h"
#include "demi/runtime/scene/EntityPresets.h"
#include "demi/runtime/scene/composition/PrefabResolver.h"

namespace demi::editor {
namespace {
std::optional<nlohmann::json>
updatedComponentOverrides(const nlohmann::json &instance,
                          const SceneValueTarget &target,
                          const std::optional<nlohmann::json> &value) {
  auto overrides = instance.value("overrides", nlohmann::json::object());

  // Clear both accepted spellings so a later dotted property cannot resurrect
  // a removed component. Unrelated entity and component overrides stay intact.
  const std::string shortPrefix =
      target.prefabEntityId + "." + target.component;
  const std::string fullPrefix =
      target.prefabEntityId + ".components." + target.component;
  for (auto entry = overrides.begin(); entry != overrides.end();) {
    const auto &key = entry.key();
    if (key == shortPrefix || key.starts_with(shortPrefix + ".") ||
        key == fullPrefix || key.starts_with(fullPrefix + "."))
      entry = overrides.erase(entry);
    else
      ++entry;
  }
  auto local = overrides.find(target.prefabEntityId);
  if (local != overrides.end() && local->is_object()) {
    auto components = local->find("components");
    if (components != local->end() && components->is_object()) {
      components->erase(target.component);
      if (components->empty())
        local->erase(components);
    }
    if (local->empty())
      overrides.erase(local);
  }
  if (value)
    overrides[target.prefabEntityId]["components"][target.component] = *value;

  std::optional<nlohmann::json> after;
  if (!overrides.empty())
    after = std::move(overrides);
  return after;
}
} // namespace

std::optional<bool>
EditorSceneDocument::prefabInheritsComponent(const SceneValueTarget &target,
                                             std::string &error) const {
  auto inherited = document_;
  auto *instance = findPrefabInstance(inherited, target.prefabInstanceId);
  if (instance == nullptr) {
    error = "The prefab instance no longer exists.";
    return std::nullopt;
  }
  const auto overrides =
      updatedComponentOverrides(*instance, target, std::nullopt);
  if (overrides)
    (*instance)["overrides"] = *overrides;
  else
    instance->erase("overrides");
  const auto expansion =
      runtime::composition::expandScene(path_, inherited, false);
  if (!expansion.document) {
    error = "Could not resolve inherited component values.";
    return std::nullopt;
  }
  const auto *source = findEntity(*expansion.document, target.entityId);
  if (source == nullptr) {
    error = "The prefab entity has no authored source.";
    return std::nullopt;
  }
  return runtime::scene_loading::expandEntityPreset(*source)
      .value("components", nlohmann::json::object())
      .contains(target.component);
}

bool EditorSceneDocument::setPrefabComponentOverride(
    const SceneValueTarget &target, std::optional<nlohmann::json> value,
    std::string &error) {
  const auto *instance = findPrefabInstance(document_, target.prefabInstanceId);
  if (!target.isPrefabOverride() || target.prefabEntityId.empty() ||
      instance == nullptr ||
      runtime::scene_loading::findComponentDescriptor(target.component) ==
          nullptr) {
    error = "Select a component on a prefab instance.";
    reject(target, error);
    return false;
  }
  if (value && !value->is_object() && !value->is_null()) {
    error = "A component override must be an object or a removal.";
    reject(target, error);
    return false;
  }

  std::optional<nlohmann::json> before;
  if (instance->contains("overrides"))
    before = instance->at("overrides");
  auto after = updatedComponentOverrides(*instance, target, value);
  if (before == after) {
    clearIssue();
    return true;
  }
  return stageAndCommit(
      SetPrefabOverridesCommand{.entityId = target.entityId,
                                .instanceId = target.prefabInstanceId,
                                .before = std::move(before),
                                .after = std::move(after)},
      error);
}

} // namespace demi::editor
