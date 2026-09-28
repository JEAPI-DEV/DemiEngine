#pragma once

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>

namespace demi::runtime {

enum class AsyncWaitResult { Ready, Cancelled, Timeout };

// A one-shot native notification. Callbacks may run on the completing thread;
// consumers must marshal gameplay or Lua work onto its owning thread.
class AsyncCompletion {
  struct State;

public:
  class Subscription {
  public:
    Subscription() = default;
    ~Subscription();
    Subscription(Subscription &&other) noexcept;
    Subscription &operator=(Subscription &&other) noexcept;
    Subscription(const Subscription &) = delete;
    Subscription &operator=(const Subscription &) = delete;

    void unsubscribe() noexcept;

  private:
    friend class AsyncCompletion;
    Subscription(std::weak_ptr<State> state, std::size_t id) noexcept;

    std::weak_ptr<State> state_;
    std::size_t id_ = 0;
  };

  AsyncCompletion();
  [[nodiscard]] bool ready() const;
  // Notification-driven wait. Ready takes precedence over cancellation and
  // timeout when observed together. Non-positive timeouts never block.
  // Timeouts beyond the clock's range saturate at its maximum deadline.
  [[nodiscard]] AsyncWaitResult
  wait(std::stop_token stopToken = {},
       std::optional<std::chrono::milliseconds> timeout = std::nullopt) const;
  // If already complete, invokes callback before returning. Destroying the
  // subscription prevents callbacks that have not been taken for dispatch.
  [[nodiscard]] Subscription subscribe(std::function<void()> callback);
  void complete();

private:
  std::shared_ptr<State> state_;
};

} // namespace demi::runtime
