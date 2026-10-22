#pragma once

#include <utility>

namespace demi::editor {

// Terrain belongs to an active 3D view or explicit scene warmup. A successful
// draw is not the ownership signal: target preparation/resizing may skip it.
class EditorTerrainViewDemand {
public:
  void request3D() { requested_ = true; }
  void requestOtherContent() { requested_ = false; }
  void observeVisibility(bool visible) {
    if (visible && active_) {
      requested_ = true;
    }
  }
  [[nodiscard]] bool finishFrame() {
    active_ = std::exchange(requested_, false);
    return active_;
  }
  [[nodiscard]] bool active() const { return active_; }
  void reset() {
    requested_ = false;
    active_ = false;
  }

private:
  bool requested_ = false;
  bool active_ = false;
};

} // namespace demi::editor
