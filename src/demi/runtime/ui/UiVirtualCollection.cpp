#include "demi/runtime/ui/UiVirtualCollection.h"

#include <algorithm>
#include <cmath>
#include <limits>

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

} // namespace demi::runtime::ui
