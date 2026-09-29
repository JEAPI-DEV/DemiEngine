#pragma once

#include "editor/EditorSceneJson.h"

#include <nlohmann/json.hpp>

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace demi::runtime {
struct TerrainPatch;
}

namespace demi::editor {

struct TerrainRecipeCommand {
  SceneValueTarget target;
  // JSON Patch paths are relative to the recipe, never the scene document.
  nlohmann::json forwardPatch;
  nlohmann::json inversePatch;
  std::shared_ptr<const runtime::TerrainPatch> samplePatch;
  bool hadRecipe = true;
  // Only override-container shape changes are retained, with recipe values
  // replaced by null. No instance or terrain sample-field snapshot is stored.
  nlohmann::json prefabSourceInversePatch = nlohmann::json::array();
  // Preserve a nested recipe shadowed by an exact dotted override, if present.
  std::optional<nlohmann::json> prefabNestedRecipePatch;
};

struct SetValueCommand {
  SceneValueTarget target;
  std::optional<nlohmann::json> before;
  std::optional<nlohmann::json> after;
  bool createdComponent = false;
  bool createdComponentsContainer = false;
  // Field undo must preserve an instance-added empty component, and retain
  // the exact dotted/nested override shape rather than reconstructing it.
  std::optional<nlohmann::json> prefabOverridesBefore;
  bool preserveEmptyPrefabComponent = false;
};

struct SetValuesCommand {
  std::vector<SetValueCommand> values;
};

struct SetSceneHudCommand {
  std::optional<nlohmann::json> before;
  std::optional<nlohmann::json> after;
};

struct InsertEntityCommand {
  std::size_t index = 0;
  nlohmann::json entity;
};

struct IndexedSceneEntity {
  std::size_t index = 0;
  nlohmann::json entity;
};

struct RemoveEntitiesCommand {
  // Stored in ascending source order. Forward removal runs in reverse so the
  // original indexes remain valid; revert inserts in source order.
  std::vector<IndexedSceneEntity> entities;
};

struct DuplicateEntityCommand {
  std::size_t index = 0;
  std::vector<nlohmann::json> entities;
};

struct ReparentCommand {
  std::string entityId;
  std::string component;
  std::optional<std::string> before;
  std::optional<std::string> after;
};

// Structural edits can move subtrees between arrays. Keep their exact source
// shape for undo, including legacy flat relationships and sibling ordering.
struct EntityHierarchyCommand {
  std::string entityId;
  nlohmann::json before;
  nlohmann::json after;
  // When populated, also preserve edits to separately authored prefab instances.
  std::optional<nlohmann::json> instancesBefore = std::nullopt;
  std::optional<nlohmann::json> instancesAfter = std::nullopt;
};

struct AddComponentCommand {
  std::string entityId;
  std::string componentName;
  nlohmann::json component;
};

// Snapshot only the owning instance's overrides, preserving their source shape
// exactly across undo, including dotted property overrides.
struct SetPrefabOverridesCommand {
  std::string entityId;
  std::string instanceId;
  std::optional<nlohmann::json> before;
  std::optional<nlohmann::json> after;
};

struct RemoveComponentCommand {
  std::string entityId;
  std::string componentName;
  nlohmann::json component;
};

// A reversible authored-scene mutation. Each alternative carries exactly the
// source data its apply/revert needs. Commands are replayed in history order
// against their owning document revision and never depend on live pointers or
// selection.
using SceneCommand =
    std::variant<SetValueCommand, TerrainRecipeCommand, SetValuesCommand,
                 SetSceneHudCommand, InsertEntityCommand, RemoveEntitiesCommand,
                 DuplicateEntityCommand, ReparentCommand,
                 EntityHierarchyCommand, AddComponentCommand,
                 RemoveComponentCommand, SetPrefabOverridesCommand>;

// Applies `command` forward onto `document`, or reverts it when `forward` is
// false. Purely structural: no validation is performed here.
void applySceneCommand(nlohmann::json &document, const SceneCommand &command,
                       bool forward);

// Builds sparse recipe and source-shape deltas from a validated replacement.
TerrainRecipeCommand makeTerrainRecipeCommand(
    const nlohmann::json &before, const nlohmann::json &after,
    SceneValueTarget target,
    std::shared_ptr<const runtime::TerrainPatch> samplePatch);

// Returns the primary entity id the command affects, used to keep selection
// and preview synchronization pointed at the right authored entity.
std::string sceneCommandEntityId(const SceneCommand &command);

} // namespace demi::editor
