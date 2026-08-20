#pragma once

#include "editor/EditorSceneJson.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace demi::editor {

struct SetValueCommand {
  SceneValueTarget target;
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

struct AddComponentCommand {
  std::string entityId;
  std::string componentName;
  nlohmann::json component;
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
    std::variant<SetValueCommand, InsertEntityCommand, RemoveEntitiesCommand,
                 DuplicateEntityCommand, ReparentCommand, AddComponentCommand,
                 RemoveComponentCommand>;

// Applies `command` forward onto `document`, or reverts it when `forward` is
// false. Purely structural: no validation is performed here.
void applySceneCommand(nlohmann::json &document, const SceneCommand &command,
                       bool forward);

// Returns the primary entity id the command affects, used to keep selection
// and preview synchronization pointed at the right authored entity.
std::string sceneCommandEntityId(const SceneCommand &command);

} // namespace demi::editor
