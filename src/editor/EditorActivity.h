#pragma once

namespace demi::editor {

// A short settling period lets hover/tooltips and newly opened dock targets
// finish updating. Once settled, an unchanged authoring view needs no frames.
class EditorActivity {
public:
  void notify(double now) { awakeUntil_ = now + 0.75; }
  bool needsFrame(double now, bool continuous) {
    if (!initialized_ || continuous) {
      initialized_ = true;
      notify(now);
    }
    return now < awakeUntil_;
  }

private:
  bool initialized_ = false;
  double awakeUntil_ = 0;
};

} // namespace demi::editor
