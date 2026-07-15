#include "demi/runtime/ui/UiPresentation.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>

namespace demi::runtime::ui {

std::vector<UiPresentationNode>
buildUiPresentation(const UiDocument &document) {
  std::unordered_map<std::string, std::size_t> indexById;
  indexById.reserve(document.nodes.size());
  for (std::size_t index = 0; index < document.nodes.size(); ++index)
    indexById.emplace(document.nodes[index].id, index);

  std::vector<UiPresentationNode> result(document.nodes.size());
  std::vector<unsigned char> state(document.nodes.size(), 0);
  std::function<void(std::size_t)> resolve = [&](const std::size_t index) {
    if (state[index] == 2)
      return;
    const UiNode &node = document.nodes[index];
    result[index] = {
        .node = &node, .effectiveLayer = node.layer, .visible = node.visible};
    if (state[index] == 1) {
      result[index].visible = false;
      return;
    }

    state[index] = 1;
    if (!node.parent.empty()) {
      const auto parent = indexById.find(node.parent);
      if (parent == indexById.end()) {
        result[index].visible = false;
      } else {
        resolve(parent->second);
        if (state[parent->second] == 1) {
          result[index].visible = false;
          result[parent->second].visible = false;
        } else {
          result[index].effectiveLayer += result[parent->second].effectiveLayer;
          result[index].visible =
              result[index].visible && result[parent->second].visible;
        }
      }
    }
    state[index] = 2;
  };

  for (std::size_t index = 0; index < document.nodes.size(); ++index)
    resolve(index);

  std::stable_sort(
      result.begin(), result.end(),
      [](const UiPresentationNode &left, const UiPresentationNode &right) {
        return left.effectiveLayer < right.effectiveLayer;
      });
  return result;
}

} // namespace demi::runtime::ui
