#include "demi/runtime/ui/UiVirtualCollection.h"

#include "demi/runtime/ui/UiEventQueue.h"
#include "demi/runtime/ui/UiMutationQueue.h"
#include "demi/runtime/ui/UiStateController.h"
#include "demi/runtime/ui/UiTweenSystem.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace demi::runtime::ui {

UiVirtualRange UiVirtualCollection::visibleRange(const std::size_t itemCount,
                                                 const float itemExtent,
                                                 const float scrollOffset,
                                                 const float viewportExtent,
                                                 const std::size_t overscan) {
  if (itemCount == 0 || !std::isfinite(itemExtent) || itemExtent <= 0.0F ||
      !std::isfinite(viewportExtent) || viewportExtent <= 0.0F)
    return {};
  const float offset =
      std::max(std::isfinite(scrollOffset) ? scrollOffset : 0.0F, 0.0F);
  const long double firstValue =
      std::floor(static_cast<long double>(offset) / itemExtent);
  const std::size_t visibleFirst = firstValue >= itemCount
                                       ? itemCount
                                       : static_cast<std::size_t>(firstValue);
  const std::size_t first =
      visibleFirst > overscan ? visibleFirst - overscan : 0;
  if (first >= itemCount)
    return {itemCount, 0};
  const long double visibleCapacity =
      std::ceil(static_cast<long double>(viewportExtent) / itemExtent);
  const std::size_t boundedVisibleCapacity =
      visibleCapacity >= itemCount ? itemCount
                                   : static_cast<std::size_t>(visibleCapacity);
  const std::size_t remainingCapacity = itemCount - boundedVisibleCapacity;
  const std::size_t overscanLimit =
      remainingCapacity / 2 + remainingCapacity % 2;
  const std::size_t capacity = overscan >= overscanLimit
                                   ? itemCount
                                   : boundedVisibleCapacity + overscan * 2;
  return {first, std::min(capacity, itemCount - first)};
}

bool UiVirtualLayout::reset(const std::span<const float> itemExtents,
                            std::string &error) {
  std::vector<float> offsets;
  offsets.reserve(itemExtents.size() + 1);
  offsets.push_back(0.0F);
  double total = 0.0;
  for (const float extent : itemExtents) {
    if (!std::isfinite(extent) || extent <= 0.0F) {
      error = "Virtual item extents must be finite and positive.";
      return false;
    }
    total += static_cast<double>(extent);
    if (total > std::numeric_limits<float>::max()) {
      error = "Virtual collection extent exceeds the supported range.";
      return false;
    }
    const float nextOffset = static_cast<float>(total);
    if (nextOffset <= offsets.back()) {
      error = "Virtual item extents are too small for the collection scale.";
      return false;
    }
    offsets.push_back(nextOffset);
  }
  extents_.assign(itemExtents.begin(), itemExtents.end());
  offsets_ = std::move(offsets);
  error.clear();
  return true;
}

bool UiVirtualLayout::setItemExtent(const std::size_t index,
                                    const float itemExtent,
                                    std::string &error) {
  if (index >= extents_.size()) {
    error = "Virtual item index is out of range.";
    return false;
  }
  if (!std::isfinite(itemExtent) || itemExtent <= 0.0F) {
    error = "Virtual item extent must be finite and positive.";
    return false;
  }
  std::vector<float> nextExtents = extents_;
  nextExtents[index] = itemExtent;
  return reset(nextExtents, error);
}

UiVirtualRange UiVirtualLayout::visibleRange(const float scrollOffset,
                                             const float viewportExtent,
                                             const std::size_t overscan) const {
  if (extents_.empty() || !std::isfinite(viewportExtent) ||
      viewportExtent <= 0.0F)
    return {};
  const float offset =
      std::max(std::isfinite(scrollOffset) ? scrollOffset : 0.0F, 0.0F);
  if (offset >= offsets_.back())
    return {extents_.size(), 0};

  const auto firstBoundary =
      std::upper_bound(offsets_.begin() + 1, offsets_.end(), offset);
  const std::size_t visibleFirst =
      static_cast<std::size_t>(firstBoundary - offsets_.begin() - 1);
  const double viewportEnd =
      static_cast<double>(offset) + static_cast<double>(viewportExtent);
  const float boundedEnd = static_cast<float>(
      std::min(viewportEnd, static_cast<double>(offsets_.back())));
  const auto lastBoundary =
      std::lower_bound(offsets_.begin() + 1, offsets_.end(), boundedEnd);
  const std::size_t measuredVisibleEnd =
      static_cast<std::size_t>(lastBoundary - offsets_.begin());
  const std::size_t visibleEnd = std::max(measuredVisibleEnd, visibleFirst + 1);

  const std::size_t first =
      visibleFirst > overscan ? visibleFirst - overscan : 0;
  const std::size_t trailingItems = extents_.size() - visibleEnd;
  const std::size_t last =
      overscan >= trailingItems ? extents_.size() : visibleEnd + overscan;
  return {.first = first, .count = last - first};
}

float UiVirtualLayout::itemOffset(const std::size_t index) const {
  return index < extents_.size() ? offsets_[index] : totalExtent();
}

float UiVirtualLayout::itemExtent(const std::size_t index) const {
  return index < extents_.size() ? extents_[index] : 0.0F;
}

float UiVirtualLayout::totalExtent() const {
  return offsets_.empty() ? 0.0F : offsets_.back();
}

namespace {
bool descendantOf(const UiDocument &document, std::string id,
                  const std::string_view root) {
  std::size_t guard = 0;
  while (!id.empty() && guard++ <= document.nodes.size()) {
    if (id == root)
      return true;
    const UiNode *node = UiStateController{}.find(document, id);
    if (node == nullptr)
      return false;
    id = node->parent;
  }
  return false;
}

std::string clonedId(const std::string_view slotRoot,
                     const std::string_view templateRoot,
                     const std::string_view templateId) {
  return templateId == templateRoot ? std::string(slotRoot)
                                    : std::string(slotRoot) + "." +
                                          std::string(templateId);
}
} // namespace

bool UiVirtualRecycler::resetSlot(UiDocument &document, UiTweenSystem &tweens,
                                  const UiNodeHandle &rowTemplate, Slot &slot,
                                  std::string &error) const {
  if (!UiMutationQueue::alive(document, rowTemplate)) {
    error = "Virtual row template handle is stale.";
    return false;
  }
  UiEventQueue::cancelSubtree(document, slot.root, "virtual_row_recycled");
  tweens.cancelSubtree(document, slot.root);
  std::vector<UiNode> templates;
  std::unordered_set<std::string> expected;
  for (const auto &node : document.nodes)
    if (descendantOf(document, node.id, rowTemplate.id)) {
      templates.push_back(node);
      expected.insert(clonedId(slot.root, rowTemplate.id, node.id));
    }
  // Runtime-created row content belongs to the old binding. Remove only the
  // highest unexpected roots; their descendants leave transactionally with
  // them through the normal mutation path.
  UiMutationQueue removals;
  for (const auto &node : document.nodes) {
    if (node.id == slot.root || !descendantOf(document, node.id, slot.root) ||
        expected.contains(node.id) ||
        (!node.parent.empty() && !expected.contains(node.parent)))
      continue;
    if (const auto handle = UiMutationQueue::handle(document, node.id))
      removals.remove(*handle);
  }
  if (!removals.empty()) {
    const auto removed = removals.apply(document);
    if (!removed.applied) {
      error = removed.error;
      return false;
    }
  }
  for (const UiNode &source : templates) {
    const std::string targetId =
        clonedId(slot.root, rowTemplate.id, source.id);
    UiNode *target = UiStateController{}.find(document, targetId);
    if (target == nullptr) {
      error = "A recycled row no longer matches its template subtree.";
      return false;
    }
    const std::string preservedId = target->id;
    const std::string preservedParent = target->parent;
    *target = source;
    target->id = preservedId;
    target->parent = source.id == rowTemplate.id
                         ? preservedParent
                         : clonedId(slot.root, rowTemplate.id, source.parent);
    target->hovered = false;
    target->textEdit = {};
    document.generations[targetId] = document.nextGeneration++;
  }
  slot.key.clear();
  error.clear();
  return true;
}

UiVirtualReconcileResult UiVirtualRecycler::reconcile(
    UiDocument &document, UiTweenSystem &tweens,
    const UiNodeHandle rowTemplate,
    const std::span<const std::string> stableKeys,
    const std::span<const float> rowExtents, const float scrollOffset,
    const float viewportExtent, const std::size_t overscan) {
  UiVirtualReconcileResult result;
  if (stableKeys.size() != rowExtents.size()) {
    result.error = "Virtual row keys and extents must have the same size.";
    return result;
  }
  if (rowTemplate_ && (rowTemplate_->id != rowTemplate.id ||
                       rowTemplate_->generation != rowTemplate.generation))
    clear(document, tweens);
  rowTemplate_ = rowTemplate;
  if (!UiMutationQueue::alive(document, rowTemplate)) {
    result.error = "Virtual row template handle is stale.";
    return result;
  }
  // Fail before cancelling interaction state when an externally edited pool
  // no longer matches its template. This keeps rejected reconciliation
  // transactional from the caller's point of view.
  for (const auto &slot : slots_) {
    if (UiStateController{}.find(document, slot.root) == nullptr) {
      result.error = "Virtual row slot was removed outside its owner.";
      return result;
    }
    for (const auto &source : document.nodes)
      if (descendantOf(document, source.id, rowTemplate.id) &&
          UiStateController{}.find(
              document,
              clonedId(slot.root, rowTemplate.id, source.id)) == nullptr) {
        result.error = "A recycled row no longer matches its template subtree.";
        return result;
      }
  }
  std::unordered_set<std::string> unique;
  for (const auto &key : stableKeys)
    if (key.empty() || !unique.insert(key).second) {
      result.error = "Virtual row keys must be non-empty and unique.";
      return result;
    }
  UiVirtualLayout layout;
  if (!layout.reset(rowExtents, result.error))
    return result;
  result.range = layout.visibleRange(scrollOffset, viewportExtent, overscan);
  const std::size_t required = result.range.count;

  if (slots_.size() < required) {
    UiMutationQueue queue;
    const UiNode *templateNode =
        UiStateController{}.find(document, rowTemplate.id);
    for (std::size_t index = slots_.size(); index < required; ++index)
      queue.clone(rowTemplate,
                  collectionId_ + ".slot." + std::to_string(index),
                  templateNode->parent);
    const UiMutationResult mutation = queue.apply(document);
    if (!mutation.applied) {
      result.error = mutation.error;
      return result;
    }
    for (std::size_t index = slots_.size(); index < required; ++index)
      slots_.push_back(
          {.root = collectionId_ + ".slot." + std::to_string(index),
           .key = {}});
  }

  std::unordered_set<std::string> wanted;
  for (std::size_t index = result.range.first;
       index < result.range.first + result.range.count; ++index)
    wanted.insert(stableKeys[index]);
  for (auto &[key, slotIndex] : keyToSlot_)
    if (!wanted.contains(key)) {
      if (!resetSlot(document, tweens, rowTemplate, slots_[slotIndex],
                     result.error))
        return result;
    }
  std::erase_if(keyToSlot_,
                [&](const auto &entry) { return !wanted.contains(entry.first); });

  result.rows.reserve(required);
  for (std::size_t index = result.range.first;
       index < result.range.first + result.range.count; ++index) {
    const std::string &key = stableKeys[index];
    bool rebound = false;
    std::size_t slotIndex = 0;
    if (const auto existing = keyToSlot_.find(key);
        existing != keyToSlot_.end()) {
      slotIndex = existing->second;
    } else {
      const auto available = std::ranges::find_if(
          slots_, [](const Slot &slot) { return slot.key.empty(); });
      if (available == slots_.end()) {
        result.error = "Virtual row pool failed to provide a free slot.";
        return result;
      }
      slotIndex = static_cast<std::size_t>(available - slots_.begin());
      available->key = key;
      keyToSlot_[key] = slotIndex;
      rebound = true;
    }
    Slot &slot = slots_[slotIndex];
    UiNode *root = UiStateController{}.find(document, slot.root);
    if (root == nullptr) {
      result.error = "Virtual row slot was removed outside its owner.";
      return result;
    }
    root->visible = true;
    root->layout.position.y = layout.itemOffset(index);
    root->layout.size.y = layout.itemExtent(index);
    result.rows.push_back(
        {.key = key,
         .index = index,
         .node = *UiMutationQueue::handle(document, slot.root),
         .offset = layout.itemOffset(index),
         .extent = layout.itemExtent(index),
         .rebound = rebound});
  }
  for (auto &slot : slots_)
    if (slot.key.empty())
      if (UiNode *root = UiStateController{}.find(document, slot.root))
        root->visible = false;
  result.applied = true;
  return result;
}

void UiVirtualRecycler::clear(UiDocument &document, UiTweenSystem &tweens) {
  UiMutationQueue queue;
  for (const Slot &slot : slots_) {
    UiEventQueue::cancelSubtree(document, slot.root, "virtual_collection_clear");
    tweens.cancelSubtree(document, slot.root);
    if (const auto handle = UiMutationQueue::handle(document, slot.root))
      queue.remove(*handle);
  }
  static_cast<void>(queue.apply(document));
  slots_.clear();
  keyToSlot_.clear();
  rowTemplate_.reset();
}

bool UiVirtualRecycler::valid(const UiDocument &document) const {
  if (rowTemplate_ && !UiMutationQueue::alive(document, *rowTemplate_))
    return false;
  return std::ranges::all_of(slots_, [&](const Slot &slot) {
    return UiStateController{}.find(document, slot.root) != nullptr;
  });
}

} // namespace demi::runtime::ui
