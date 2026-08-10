#include "demi/runtime/ui/UiAccessibilityBridge.h"

namespace demi::runtime::ui {

void BufferedUiAccessibilityBridge::publish(
    const std::span<const UiAccessibilityNode> nodes,
    const std::uint64_t revision) {
  nodes_.assign(nodes.begin(), nodes.end());
  revision_ = revision;
}

std::optional<UiAccessibilityAction>
BufferedUiAccessibilityBridge::pollAction() {
  if (actions_.empty())
    return std::nullopt;
  UiAccessibilityAction result = std::move(actions_.front());
  actions_.erase(actions_.begin());
  return result;
}

void BufferedUiAccessibilityBridge::pushAction(UiAccessibilityAction action) {
  actions_.push_back(std::move(action));
}

void PlatformUiAccessibilityBridge::publish(
    const std::span<const UiAccessibilityNode> nodes,
    const std::uint64_t revision) {
  std::scoped_lock lock(mutex_);
  nodes_.assign(nodes.begin(), nodes.end());
  revision_ = revision;
}

std::optional<UiAccessibilityAction>
PlatformUiAccessibilityBridge::pollAction() {
  std::scoped_lock lock(mutex_);
  if (actions_.empty())
    return std::nullopt;
  UiAccessibilityAction result = std::move(actions_.front());
  actions_.pop_front();
  return result;
}

void PlatformUiAccessibilityBridge::submitAction(
    UiAccessibilityAction action) {
  std::scoped_lock lock(mutex_);
  actions_.push_back(std::move(action));
}

std::vector<UiAccessibilityNode>
PlatformUiAccessibilityBridge::snapshot() const {
  std::scoped_lock lock(mutex_);
  return nodes_;
}

std::uint64_t PlatformUiAccessibilityBridge::revision() const {
  std::scoped_lock lock(mutex_);
  return revision_;
}

void PlatformUiAccessibilityBridge::setCanvasSize(const Vec2 size) {
  std::scoped_lock lock(mutex_);
  canvasSize_ = size;
}

Vec2 PlatformUiAccessibilityBridge::canvasSize() const {
  std::scoped_lock lock(mutex_);
  return canvasSize_;
}

void PlatformUiAccessibilityBridge::clear() {
  std::scoped_lock lock(mutex_);
  nodes_.clear();
  actions_.clear();
  ++revision_;
}

PlatformUiAccessibilityBridge &platformUiAccessibilityBridge() {
  static PlatformUiAccessibilityBridge bridge;
  return bridge;
}

void UiAccessibilityBridgeController::update(UiDocument &document) {
  while (const auto action = bridge_.pollAction())
    static_cast<void>(actions_.perform(document, *action));
  const auto snapshot = UiAccessibilityTree::snapshot(document);
  if (auto *platform = dynamic_cast<PlatformUiAccessibilityBridge *>(&bridge_))
    platform->setCanvasSize(document.canvasSize);
  bridge_.publish(snapshot, ++revision_);
}

} // namespace demi::runtime::ui
