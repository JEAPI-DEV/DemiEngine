#pragma once
#include <cstddef>
namespace demi::runtime::ui {
struct UiVirtualRange { std::size_t first = 0; std::size_t count = 0; };
class UiVirtualCollection {
public:
  [[nodiscard]] static UiVirtualRange visibleRange(
      std::size_t itemCount, float itemExtent, float scrollOffset,
      float viewportExtent, std::size_t overscan = 2);
};
} // namespace demi::runtime::ui
