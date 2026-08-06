#pragma once
#include "demi/runtime/ui/UiModel.h"
#include <cstdint>
#include <string>
#include <vector>
namespace demi::runtime::ui {
enum class UiTweenProperty { Opacity, PositionX, PositionY, Scale };
struct UiTweenHandle { std::uint64_t value = 0; explicit operator bool() const { return value != 0; } };
class UiTweenSystem {
public:
  [[nodiscard]] UiTweenHandle start(UiDocument &document, UiNodeHandle node,
                                    UiTweenProperty property, float target,
                                    float duration);
  bool cancel(UiTweenHandle handle);
  void update(UiDocument &document, float dt);
  void setReducedMotion(bool enabled) { reducedMotion_ = enabled; }
  [[nodiscard]] std::size_t activeCount() const { return tweens_.size(); }
private:
  struct Tween { UiTweenHandle handle; UiNodeHandle node; UiTweenProperty property; float from; float to; float duration; float elapsed = 0.0F; };
  std::vector<Tween> tweens_;
  std::uint64_t next_ = 1;
  bool reducedMotion_ = false;
};
} // namespace demi::runtime::ui
