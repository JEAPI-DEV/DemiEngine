#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace demi::editor {

// Addresses a single authored value inside a scene document. An empty
// `component` addresses an entity-level field; otherwise `field` lives inside
// the named component object.
struct SceneValueTarget {
  std::string entityId;
  std::string component;
  std::string field;

  friend bool operator==(const SceneValueTarget &,
                         const SceneValueTarget &) = default;
};

// Pure read/write helpers over authored scene JSON. These functions own no
// document state and perform no validation, so they stay trivially testable
// and reusable by both the document and any future authored-format adapter.
nlohmann::json *findEntity(nlohmann::json &document, std::string_view id);
const nlohmann::json *findEntity(const nlohmann::json &document,
                                 std::string_view id);

nlohmann::json *entitiesArray(nlohmann::json &document);
const nlohmann::json *entitiesArray(const nlohmann::json &document);

std::optional<std::size_t> entityIndex(const nlohmann::json &document,
                                       std::string_view id);

nlohmann::json *findComponent(nlohmann::json &entity, std::string_view name);
const nlohmann::json *findComponent(const nlohmann::json &entity,
                                    std::string_view name);

nlohmann::json *valueInDocument(nlohmann::json &document,
                                const SceneValueTarget &target);
const nlohmann::json *valueInDocument(const nlohmann::json &document,
                                      const SceneValueTarget &target);

// Returns the authored id of the Transform2D/Transform3D parent, or an empty
// string when the entity is at the root or has no transform component.
std::string transformParentId(const nlohmann::json &entity);

// Returns "Transform3D", "Transform2D", or nullptr depending on which transform
// component the entity authors first.
const char *transformComponentName(const nlohmann::json &entity);

// Returns the stable ids of an entity and every descendant reachable through
// authored transform parents, in pre-order starting with `rootId`.
std::vector<std::string> collectSubtreeIds(const nlohmann::json &document,
                                           std::string_view rootId);

// Generates an id derived from `base` that collides with neither the document
// nor any id already placed in `reserved`.
std::string uniqueEntityId(
    const nlohmann::json &document, std::string_view base,
    const std::unordered_set<std::string> &reserved = {});

// Rewrites transform parent references that point into `remap` so a duplicated
// subtree keeps its internal hierarchy intact while external parents survive.
void remapParentReferences(nlohmann::json &entity,
                           const std::unordered_map<std::string, std::string>
                               &remap);

} // namespace demi::editor
