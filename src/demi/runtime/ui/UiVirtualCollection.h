#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace demi::runtime::ui {

struct UiVirtualRange {
  std::size_t first = 0;
  std::size_t count = 0;
};

class UiVirtualCollection {
public:
  [[nodiscard]] static UiVirtualRange
  visibleRange(std::size_t itemCount, float itemExtent, float scrollOffset,
               float viewportExtent, std::size_t overscan = 2);
};

class UiVirtualLayout {
public:
  [[nodiscard]] bool reset(std::span<const float> itemExtents,
                           std::string &error);
  [[nodiscard]] bool setItemExtent(std::size_t index, float itemExtent,
                                   std::string &error);

  [[nodiscard]] UiVirtualRange visibleRange(float scrollOffset,
                                            float viewportExtent,
                                            std::size_t overscan = 2) const;
  [[nodiscard]] float itemOffset(std::size_t index) const;
  [[nodiscard]] float itemExtent(std::size_t index) const;
  [[nodiscard]] float totalExtent() const;
  [[nodiscard]] std::size_t itemCount() const { return extents_.size(); }

private:
  std::vector<float> extents_;
  std::vector<float> offsets_{0.0F};
};

} // namespace demi::runtime::ui
