#include "demi/runtime/ui/UiMutationQueue.h"

#include <algorithm>
#include <cctype>
#include <ranges>
#include <unordered_set>

namespace demi::runtime::ui {
namespace {
UiNode *find(UiDocument &document, const std::string_view id) {
  const auto value = std::ranges::find(document.nodes, id, &UiNode::id);
  return value == document.nodes.end() ? nullptr : &*value;
}
const UiNode *find(const UiDocument &document, const std::string_view id) {
  const auto value = std::ranges::find(document.nodes, id, &UiNode::id);
  return value == document.nodes.end() ? nullptr : &*value;
}
bool validId(const std::string_view id) {
  return !id.empty() && std::ranges::all_of(id, [](const unsigned char value) {
    return std::isalnum(value) != 0 || value == '_' || value == '-' ||
           value == '.' || value == ':';
  });
}
bool isDescendant(const UiDocument &document, std::string id,
                  const std::string_view ancestor) {
  std::unordered_set<std::string> visited;
  while (!id.empty() && visited.insert(id).second) {
    if (id == ancestor) return true;
    const UiNode *node = find(document, id);
    if (node == nullptr) return false;
    id = node->parent;
  }
  return false;
}
void eraseSubtree(UiDocument &document, const std::string &root) {
  std::unordered_set<std::string> removed{root};
  for (bool changed = true; changed;) {
    changed = false;
    for (const auto &node : document.nodes)
      if (removed.contains(node.parent) && removed.insert(node.id).second)
        changed = true;
  }
  std::erase_if(document.nodes,
                [&](const UiNode &node) { return removed.contains(node.id); });
  for (const auto &id : removed) document.generations.erase(id);
  if (removed.contains(document.focusedId)) document.focusedId.clear();
  if (removed.contains(document.pointerCaptureId))
    document.pointerCaptureId.clear();
  std::erase_if(document.pointerCaptures,
                [&](const auto &capture) { return removed.contains(capture.second); });
}
} // namespace

void UiMutationQueue::create(std::string parent, UiNode node) {
  mutations_.push_back(Create{std::move(parent), std::move(node)});
}
void UiMutationQueue::clone(UiNodeHandle source, std::string newRootId,
                            std::string parent) {
  mutations_.push_back(Clone{std::move(source), std::move(newRootId),
                             std::move(parent)});
}
void UiMutationQueue::remove(UiNodeHandle node) {
  mutations_.push_back(Remove{std::move(node)});
}
void UiMutationQueue::reparent(UiNodeHandle node, std::string parent) {
  mutations_.push_back(Reparent{std::move(node), std::move(parent)});
}
void UiMutationQueue::clearChildren(std::string parent) {
  mutations_.push_back(Clear{std::move(parent)});
}

void UiMutationQueue::initializeGenerations(UiDocument &document) {
  for (const auto &node : document.nodes)
    if (!document.generations.contains(node.id))
      document.generations[node.id] = document.nextGeneration++;
}

std::optional<UiNodeHandle>
UiMutationQueue::handle(const UiDocument &document, const std::string_view id) {
  const auto generation = document.generations.find(std::string(id));
  if (generation == document.generations.end() || find(document, id) == nullptr)
    return std::nullopt;
  return UiNodeHandle{std::string(id), generation->second};
}

bool UiMutationQueue::alive(const UiDocument &document,
                            const UiNodeHandle &handle) {
  const auto generation = document.generations.find(handle.id);
  return generation != document.generations.end() &&
         generation->second == handle.generation && find(document, handle.id);
}

UiMutationResult UiMutationQueue::apply(UiDocument &document) {
  initializeGenerations(document);
  UiDocument result = document;
  const auto fail = [&](std::string error) {
    mutations_.clear();
    return UiMutationResult{false, std::move(error)};
  };
  for (const Mutation &operation : mutations_) {
    if (const auto *value = std::get_if<Create>(&operation)) {
      if (!validId(value->node.id)) return fail("Invalid UI node id.");
      if (find(result, value->node.id)) return fail("Duplicate UI node id: " + value->node.id);
      if (!value->parent.empty() && !find(result, value->parent))
        return fail("Missing UI parent: " + value->parent);
      UiNode node = value->node;
      node.parent = value->parent;
      if (const auto style = result.styles.find(node.style);
          style != result.styles.end()) {
        node.color = style->second.color;
        node.backgroundColor = style->second.backgroundColor;
        if (node.layout.padding.left == 0.0F && node.layout.padding.top == 0.0F &&
            node.layout.padding.right == 0.0F && node.layout.padding.bottom == 0.0F)
          node.layout.padding = style->second.padding;
        if (node.layout.gap == 0.0F) node.layout.gap = style->second.gap;
      }
      result.generations[node.id] = result.nextGeneration++;
      result.nodes.push_back(std::move(node));
    } else if (const auto *value = std::get_if<Clone>(&operation)) {
      if (!alive(result, value->source)) return fail("Stale UI clone handle.");
      if (!validId(value->root) || find(result, value->root))
        return fail("Invalid or duplicate cloned UI root.");
      if (!value->parent.empty() && !find(result, value->parent))
        return fail("Missing clone parent: " + value->parent);
      std::vector<UiNode> copies;
      for (const auto &node : result.nodes)
        if (isDescendant(result, node.id, value->source.id)) copies.push_back(node);
      for (auto &node : copies) {
        const std::string oldId = node.id;
        node.id = oldId == value->source.id ? value->root
                                            : value->root + "." + oldId;
        node.parent = oldId == value->source.id
                          ? value->parent
                      : node.parent == value->source.id
                          ? value->root
                          : value->root + "." + node.parent;
        if (find(result, node.id)) return fail("Cloned UI id collision: " + node.id);
      }
      for (auto &node : copies) {
        result.generations[node.id] = result.nextGeneration++;
        result.nodes.push_back(std::move(node));
      }
    } else if (const auto *value = std::get_if<Remove>(&operation)) {
      if (!alive(result, value->node)) return fail("Stale UI remove handle.");
      eraseSubtree(result, value->node.id);
    } else if (const auto *value = std::get_if<Reparent>(&operation)) {
      if (!alive(result, value->node)) return fail("Stale UI reparent handle.");
      if (!value->parent.empty() && !find(result, value->parent))
        return fail("Missing UI parent: " + value->parent);
      if (value->parent == value->node.id ||
          isDescendant(result, value->parent, value->node.id))
        return fail("UI reparent would create a cycle.");
      find(result, value->node.id)->parent = value->parent;
    } else if (const auto *value = std::get_if<Clear>(&operation)) {
      if (!value->parent.empty() && !find(result, value->parent))
        return fail("Missing UI parent: " + value->parent);
      std::vector<std::string> children;
      for (const auto &node : result.nodes)
        if (node.parent == value->parent) children.push_back(node.id);
      for (const auto &id : children) eraseSubtree(result, id);
    }
  }
  document = std::move(result);
  mutations_.clear();
  return {true, {}};
}

} // namespace demi::runtime::ui
