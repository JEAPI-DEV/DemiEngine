#include "editor/EditorEntityHierarchy.h"
#include "editor/EditorSceneJson.h"

namespace demi::editor {
void eraseEntitySubtrees(nlohmann::json &entities,
                         const std::unordered_set<std::string> &ids) {
  for (auto it = entities.begin(); it != entities.end();) {
    if (ids.contains(it->value("id", ""))) {
      it = entities.erase(it);
    } else {
      if (it->contains("children")) {
        eraseEntitySubtrees((*it)["children"], ids);
        if ((*it)["children"].empty())
          it->erase("children");
      }
      ++it;
    }
  }
}
void insertEntityUnder(nlohmann::json &document, nlohmann::json entity,
                       const std::string &parent) {
  auto *owner = parent.empty() ? nullptr : findEntity(document, parent);
  const char *transform = transformComponentName(entity);
  if (!transform && owner)
    transform = transformComponentName(*owner);
  if (transform) {
    auto &value = entity["components"][transform];
    if (!value.is_object())
      value = nlohmann::json::object();
    if (owner || parent.empty())
      value.erase("parent");
    else
      value["parent"] = parent;
  }
  if (owner) {
    auto &children = (*owner)["children"];
    if (!children.is_array())
      children = nlohmann::json::array();
    children.push_back(std::move(entity));
  } else {
    document["entities"].push_back(std::move(entity));
  }
}
} // namespace demi::editor
