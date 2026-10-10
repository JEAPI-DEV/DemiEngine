#include "demi/runtime/scene/composition/PrefabResolver.h"
#include "editor/EditorEntityHierarchy.h"
#include "editor/EditorSceneDocument.h"
#include "editor/EditorSceneJson.h"
namespace demi::editor {
bool EditorSceneDocument::unpackPrefab(std::string_view selection,
                                       std::string &error) {
  using Json = nlohmann::json;
  std::string instanceId(selection);
  if (!findPrefabInstance(document_, instanceId)) {
    const auto origin =
        runtime::composition::prefabEntityOrigin(document_, selection);
    if (!origin) {
      error = "Select a prefab instance or one of its entities.";
      return false;
    }
    instanceId = origin->instanceId;
  }
  const auto expanded =
      runtime::composition::expandSceneForAuthoring(path_, document_);
  if (!expanded.document) {
    error = expanded.diagnostics.empty() ? "Could not resolve the prefab."
                                         : expanded.diagnostics.front().message;
    return false;
  }
  runtime::composition::PrefabOriginIndex origins(document_);
  Json owned = Json::array();
  for (const auto &entity : expanded.document->at("entities")) {
    const auto origin = origins.find(entity.at("id").get<std::string>());
    if (origin && origin->instanceId == instanceId)
      owned.push_back(entity);
  }
  if (owned.empty()) {
    error = "The prefab has no resolved authored entities.";
    return false;
  }
  auto local = nestLocalEntityLinks(std::move(owned));
  auto staged = document_;
  const auto replace = [&](auto &&self, Json &entities) -> bool {
    for (auto it = entities.begin(); it != entities.end(); ++it) {
      if (it->value("id", std::string{}) == instanceId &&
          it->contains("prefab")) {
        const auto index = it - entities.begin();
        entities.erase(it);
        entities.insert(entities.begin() + index, local.begin(), local.end());
        return true;
      }
      if (it->contains("children") && self(self, (*it)["children"]))
        return true;
    }
    return false;
  };
  const bool nested = replace(replace, staged["entities"]);
  EntityHierarchyCommand command{.entityId = instanceId,
                                 .before = document_["entities"]};
  if (!nested) {
    if (!staged.contains("instances")) {
      error = "The source instance is unavailable.";
      return false;
    }
    command.instancesBefore = staged["instances"];
    auto &instances = staged["instances"];
    for (auto it = instances.begin(); it != instances.end(); ++it)
      if (it->value("id", std::string{}) == instanceId) {
        instances.erase(it);
        break;
      }
    for (auto &entity : local)
      staged["entities"].push_back(std::move(entity));
    command.instancesAfter = instances;
  }
  command.after = staged["entities"];
  return stageAndCommit(std::move(command), error);
}
} // namespace demi::editor
