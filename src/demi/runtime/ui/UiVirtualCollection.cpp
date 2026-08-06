#include "demi/runtime/ui/UiVirtualCollection.h"
#include <algorithm>
#include <cmath>
namespace demi::runtime::ui {
UiVirtualRange UiVirtualCollection::visibleRange(
    const std::size_t itemCount, const float itemExtent,
    const float scrollOffset, const float viewportExtent,
    const std::size_t overscan) {
  if (itemCount == 0 || !std::isfinite(itemExtent) || itemExtent <= 0.0F ||
      !std::isfinite(viewportExtent) || viewportExtent <= 0.0F)
    return {};
  const float offset = std::max(std::isfinite(scrollOffset) ? scrollOffset : 0.0F, 0.0F);
  const std::size_t visibleFirst = static_cast<std::size_t>(offset / itemExtent);
  const std::size_t first = visibleFirst > overscan ? visibleFirst - overscan : 0;
  if (first >= itemCount) return {itemCount, 0};
  const std::size_t capacity = static_cast<std::size_t>(
                                   std::ceil(viewportExtent / itemExtent)) +
                               overscan * 2;
  return {first, std::min(capacity, itemCount - first)};
}
} // namespace demi::runtime::ui
