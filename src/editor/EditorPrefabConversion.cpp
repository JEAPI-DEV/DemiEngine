#include "demi/runtime/scene/composition/EntityHierarchy.h"
#include "editor/EditorEntityHierarchy.h"
#include "editor/EditorSceneDocument.h"
#include "editor/EditorSceneJson.h"

namespace demi::editor {
bool EditorSceneDocument::replaceHierarchyWithPrefab(
    std::string_view selectedId, std::string_view prefabReference,
    nlohmann::json entityIds, std::string &error) {
  try {
    auto staged = document_;
    auto *source = findEntity(staged, selectedId);
    bool legacy = false;
    if (!source) {
      source = findPrefabInstance(staged, selectedId);
      legacy = source != nullptr;
    }
    if (!source) {
      error = "Select an authored hierarchy before creating a prefab.";
      return false;
    }
    const auto subtree = collectSubtreeIds(document_, selectedId);
    const std::string instanceId =
        uniqueEntityId(document_, std::string(selectedId) + "_instance");
    nlohmann::json instance{{"id", instanceId},
                            {"prefab", prefabReference},
                            {"entity_ids", std::move(entityIds)}};
    if (source->contains("name"))
      instance["name"] = source->at("name");
    const auto parent = transformParentId(*source);
    if (!parent.empty())
      if (const auto *domain = transformComponentName(*source))
        instance["components"][domain]["parent"] = parent;
    *source = std::move(instance);
    const std::unordered_set<std::string> removed(subtree.begin(),
                                                  subtree.end());
    eraseEntitySubtrees(staged["entities"], removed);
    EntityHierarchyCommand command{.entityId = instanceId,
                                   .before = document_["entities"],
                                   .after = staged["entities"]};
    if (legacy) {
      command.instancesBefore = document_.at("instances");
      command.instancesAfter = staged.at("instances");
    }
    return stageAndCommit(std::move(command), error);
  } catch (const std::exception &failure) {
    error = failure.what();
    return false;
  }
}
} // namespace demi::editor
