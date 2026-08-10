#pragma once

#include "demi/runtime/ui/UiAccessibilityActions.h"
#include "demi/runtime/ui/UiAccessibilityTree.h"

#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace demi::runtime::ui {

// Platform adapters consume this stable snapshot/action boundary. They never
// inspect UiDocument or renderer state directly.
class UiAccessibilityBridge {
public:
  virtual ~UiAccessibilityBridge() = default;
  virtual void publish(std::span<const UiAccessibilityNode> nodes,
                       std::uint64_t revision) = 0;
  [[nodiscard]] virtual std::optional<UiAccessibilityAction>
  pollAction() = 0;
};

class BufferedUiAccessibilityBridge : public UiAccessibilityBridge {
public:
  void publish(std::span<const UiAccessibilityNode> nodes,
               std::uint64_t revision) override;
  [[nodiscard]] std::optional<UiAccessibilityAction> pollAction() override;
  void pushAction(UiAccessibilityAction action);
  [[nodiscard]] std::span<const UiAccessibilityNode> nodes() const {
    return nodes_;
  }
  [[nodiscard]] std::uint64_t revision() const { return revision_; }

private:
  std::vector<UiAccessibilityNode> nodes_;
  std::vector<UiAccessibilityAction> actions_;
  std::uint64_t revision_ = 0;
};

// Thread-safe boundary consumed by the Linux/Android native host. The runtime
// remains the sole owner of semantic state; platform callbacks enqueue the
// same typed actions used by mouse, keyboard, controller, and touch input.
class PlatformUiAccessibilityBridge final : public UiAccessibilityBridge {
public:
  void publish(std::span<const UiAccessibilityNode> nodes,
               std::uint64_t revision) override;
  [[nodiscard]] std::optional<UiAccessibilityAction> pollAction() override;
  void submitAction(UiAccessibilityAction action);
  [[nodiscard]] std::vector<UiAccessibilityNode> snapshot() const;
  [[nodiscard]] std::uint64_t revision() const;
  void setCanvasSize(Vec2 size);
  [[nodiscard]] Vec2 canvasSize() const;
  void clear();

private:
  mutable std::mutex mutex_;
  std::vector<UiAccessibilityNode> nodes_;
  std::deque<UiAccessibilityAction> actions_;
  std::uint64_t revision_ = 0;
  Vec2 canvasSize_{960.0F, 540.0F};
};

[[nodiscard]] PlatformUiAccessibilityBridge &platformUiAccessibilityBridge();

class UiAccessibilityBridgeController {
public:
  explicit UiAccessibilityBridgeController(UiAccessibilityBridge &bridge)
      : bridge_(bridge) {}
  void update(UiDocument &document);
  [[nodiscard]] std::uint64_t revision() const { return revision_; }

private:
  UiAccessibilityBridge &bridge_;
  UiAccessibilityActions actions_;
  std::uint64_t revision_ = 0;
};

} // namespace demi::runtime::ui
