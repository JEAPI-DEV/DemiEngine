#include "demi/runtime/ui/UiActionController.h"
#include "demi/runtime/ui/UiLayoutEngine.h"
#include "demi/runtime/ui/UiStateController.h"
#include "editor/EditorHudDocument.h"
#include <algorithm>

namespace demi::editor {
bool EditorHudDocument::previewNodeAction(std::string_view id) {
  const auto *node = runtime::ui::UiStateController{}.find(preview_, id);
  while (node && node->action.empty() && !node->parent.empty())
    node = runtime::ui::UiStateController{}.find(preview_, node->parent);
  if (!node || node->action.empty())
    return false;
  std::unordered_map<std::string, bool> before;
  for (const auto &item : preview_.nodes)
    before[item.id] = item.visible;
  if (!runtime::ui::UiActionController{}.apply(preview_, node->action))
    return false;
  for (const auto &item : preview_.nodes)
    if (before.at(item.id) != item.visible)
      previewVisibility_[item.id] = item.visible;
  preview_.events.clear();
  runtime::ui::UiLayoutEngine{}.layout(preview_, preview_.canvasSize);
  return true;
}
void EditorHudDocument::applyPreviewState() {
  std::erase_if(previewVisibility_, [&](const auto &item) {
    return !runtime::ui::UiStateController{}.find(preview_, item.first);
  });
  for (auto &node : preview_.nodes)
    if (const auto found = previewVisibility_.find(node.id);
        found != previewVisibility_.end())
      node.visible = found->second;
  runtime::ui::UiLayoutEngine{}.layout(preview_, preview_.canvasSize);
}
bool EditorHudDocument::resetPreviewState(std::string &error) {
  previewVisibility_.clear();
  return rebuild(error);
}

} // namespace demi::editor
