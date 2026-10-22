#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

namespace demi::editor {

// Display bounds only: never modifies samples. Expansion is immediate; a lower
// range must remain useful for two seconds before we shrink to a rounded bound.
class EditorProfilerScale {
public:
  explicit EditorProfilerScale(double initialSpan) : span_(initialSpan) {}

  [[nodiscard]] double span() const { return span_; }
  [[nodiscard]] bool automatic() const { return automatic_; }

  void setAutomatic(bool value) {
    automatic_ = value;
    resetObservation();
  }

  void setManualSpan(double value) {
    if (std::isfinite(value)) {
      span_ = std::clamp(value, 0.1, 10000.0);
    }
    automatic_ = false;
    resetObservation();
  }

  void resetSession(double initialSpan) {
    if (automatic_) {
      span_ = initialSpan;
    }
    resetObservation();
  }

  void resetObservation() {
    lowerSince_.reset();
    lowerMaximum_ = 0.0;
  }

  void observe(double extent, double now) {
    if (!automatic_ || !std::isfinite(extent) || !std::isfinite(now)) {
      return;
    }
    extent = std::max(0.1, extent);
    if (extent > span_) {
      span_ = roundedBound(extent * 1.1);
      resetObservation();
      return;
    }
    // A substantial dead band prevents rescaling around a single frame budget.
    if (roundedBound(extent * 1.1) > span_ * 0.65) {
      resetObservation();
      return;
    }
    if (!lowerSince_ || now < *lowerSince_) {
      lowerSince_ = now;
      lowerMaximum_ = extent;
    } else {
      lowerMaximum_ = std::max(lowerMaximum_, extent);
    }
    if (now - *lowerSince_ >= 2.0) {
      span_ = roundedBound(lowerMaximum_ * 1.1);
      resetObservation();
    }
  }

private:
  static double roundedBound(double value) {
    const double magnitude = std::pow(10.0, std::floor(std::log10(value)));
    for (const double step : {1.0, 1.25, 1.5, 2.0, 2.5, 3.0,
                              4.0, 5.0, 6.0, 8.0, 10.0}) {
      if (step * magnitude >= value) {
        return step * magnitude;
      }
    }
    return 10.0 * magnitude;
  }

  double span_;
  bool automatic_ = true;
  std::optional<double> lowerSince_;
  double lowerMaximum_ = 0.0;
};

} // namespace demi::editor
