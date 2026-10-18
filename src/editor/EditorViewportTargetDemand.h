#pragma once

#include <array>
#include <cstdint>

namespace demi::editor {

// ImGui publishes view sizes after target preparation has already run. Track
// that handoff explicitly so a new/resized/closed view gets its preparation
// frame even when document focus and input have already settled.
class EditorViewportTargetDemand {
public:
  void observe(std::uint16_t width, std::uint16_t height) {
    requested_ = dimensions(width, height);
  }

  // An unsuccessful attempt also consumes its demand. Retrying an unchanged
  // failing allocation must wait for new activity, not keep the idle loop hot.
  void recordAttempt(std::uint16_t width, std::uint16_t height) {
    attempted_ = dimensions(width, height);
  }

  void clear() {
    requested_ = {};
    attempted_ = {};
  }

  [[nodiscard]] bool needsPreparation() const {
    return requested_ != attempted_;
  }

private:
  static std::array<std::uint16_t, 2> dimensions(std::uint16_t width,
                                                 std::uint16_t height) {
    if (width == 0 || height == 0) {
      return {};
    }
    return {width, height};
  }
  std::array<std::uint16_t, 2> requested_{};
  std::array<std::uint16_t, 2> attempted_{};
};

} // namespace demi::editor
