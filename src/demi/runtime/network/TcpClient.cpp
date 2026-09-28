#include "demi/runtime/network/TcpClient.h"
#include "demi/runtime/concurrency/AsyncCompletion.h"

#include <uv.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <climits>
#include <deque>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>

namespace demi::runtime {
namespace {

using Clock = std::chrono::steady_clock;

TcpResponse failure(const char *code, const char *message) {
  TcpResponse result;
  result.errorCode = code;
  result.error = message;
  return result;
}

Clock::time_point deadlineFor(int timeoutMs) {
  return timeoutMs == 0 ? Clock::time_point::max()
                        : Clock::now() + std::chrono::milliseconds(timeoutMs);
}

} // namespace

struct TcpOperation::Impl {
  mutable std::mutex mutex;
  std::optional<TcpResponse> result;
  std::shared_ptr<AsyncCompletion> signal = std::make_shared<AsyncCompletion>();
  std::function<void()> wake;

  bool settle(TcpResponse response) {
    {
      std::lock_guard lock(mutex);
      if (result)
        return false;
      result = std::move(response);
    }
    signal->complete();
    return true;
  }

  bool ready() const {
    std::lock_guard lock(mutex);
    return result.has_value();
  }
};

TcpOperation::TcpOperation(std::shared_ptr<Impl> impl)
    : impl_(std::move(impl)) {}

std::shared_ptr<AsyncCompletion> TcpOperation::completion() const {
  return impl_->signal;
}

std::optional<TcpResponse> TcpOperation::response() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->result;
}

void TcpOperation::cancel() {
  if (impl_->settle(failure("cancelled", "TCP operation cancelled")) &&
      impl_->wake)
    impl_->wake();
}

struct TcpHandle;

struct TcpConnection::State {
  std::atomic<bool> isOpen = false;
  std::weak_ptr<TcpReactor> reactor;
  TcpHandle *handle = nullptr; // Reactor thread only.
  std::string buffered;
  bool eof = false;
  std::shared_ptr<TcpOperation::Impl> pendingRead;
  std::size_t pendingReadMax = 0;
  Clock::time_point readDeadline = Clock::time_point::max();
};

struct TcpHandle {
  uv_tcp_t socket{};
  std::shared_ptr<TcpConnection::State> state;
};

struct TcpReactor : std::enable_shared_from_this<TcpReactor> {
  struct Attempt {
    uv_getaddrinfo_t resolver{};
    uv_connect_t connector{};
    std::shared_ptr<TcpOperation::Impl> operation;
    std::string host;
    std::string service;
    Clock::time_point deadline;
    TcpHandle *handle = nullptr;
    bool resolving = false;
    bool connecting = false;
    addrinfo *addresses = nullptr;
    addrinfo *nextAddress = nullptr;

    ~Attempt() {
      if (addresses)
        uv_freeaddrinfo(addresses);
    }
  };

  struct Write {
    uv_write_t request{};
    std::shared_ptr<TcpConnection::State> connection;
    std::shared_ptr<TcpOperation::Impl> operation;
    std::string bytes;
    Clock::time_point deadline;
  };

  explicit TcpReactor(std::size_t maxBufferedBytes)
      : maxBufferedBytes(maxBufferedBytes) {
    if (uv_loop_init(&loop) != 0)
      throw std::runtime_error("TCP loop initialization failed");
    if (uv_async_init(&loop, &wakeup, onWake) != 0) {
      uv_loop_close(&loop);
      throw std::runtime_error("TCP wakeup initialization failed");
    }
    wakeup.data = this;
    if (uv_timer_init(&loop, &clock) != 0) {
      uv_close(reinterpret_cast<uv_handle_t *>(&wakeup), nullptr);
      uv_run(&loop, UV_RUN_DEFAULT);
      uv_loop_close(&loop);
      throw std::runtime_error("TCP timer initialization failed");
    }
    clock.data = this;
  }

  ~TcpReactor() { shutdown(); }

  void start() {
    worker = std::thread([self = shared_from_this()] {
      uv_run(&self->loop, UV_RUN_DEFAULT);
      uv_loop_close(&self->loop);
    });
  }

  bool post(std::function<void()> command) {
    std::lock_guard lock(mutex);
    if (stopping)
      return false;
    commands.push_back(std::move(command));
    uv_async_send(&wakeup);
    return true;
  }

  void notify() {
    std::lock_guard lock(mutex);
    if (!stopping)
      uv_async_send(&wakeup);
  }

  void shutdown() {
    std::lock_guard joinLock(shutdownMutex);
    if (finished)
      return;
    {
      std::lock_guard lock(mutex);
      if (!stopping) {
        stopping = true;
        commands.push_back([this] { stopOnLoop(); });
        uv_async_send(&wakeup);
      }
    }
    if (worker.joinable()) {
      worker.join();
    } else {
      // Covers failure to create the thread after libuv initialization.
      uv_run(&loop, UV_RUN_DEFAULT);
      uv_loop_close(&loop);
    }
    finished = true;
  }

  std::shared_ptr<TcpOperation::Impl> makeOperation() {
    auto operation = std::make_shared<TcpOperation::Impl>();
    std::weak_ptr<TcpReactor> weak = shared_from_this();
    operation->wake = [weak] {
      if (auto reactor = weak.lock())
        reactor->notify();
    };
    return operation;
  }

  static void onWake(uv_async_t *async) {
    auto &self = *static_cast<TcpReactor *>(async->data);
    std::deque<std::function<void()>> work;
    {
      std::lock_guard lock(self.mutex);
      work.swap(self.commands);
    }
    for (auto &command : work)
      command();
    if (!self.stopping.load()) {
      self.sweep();
      self.rearmDeadline();
    }
  }

  static void onTick(uv_timer_t *timer) {
    auto &self = *static_cast<TcpReactor *>(timer->data);
    self.sweep();
    self.rearmDeadline();
  }

  void rearmDeadline() {
    auto nearest = Clock::time_point::max();
    for (const auto &[_, attempt] : attempts) {
      if (!attempt->operation->ready())
        nearest = std::min(nearest, attempt->deadline);
    }
    for (const auto &[_, state] : connections) {
      if (state->pendingRead && !state->pendingRead->ready())
        nearest = std::min(nearest, state->readDeadline);
    }
    for (const auto &[_, write] : writes) {
      if (!write->operation->ready())
        nearest = std::min(nearest, write->deadline);
    }
    if (nearest == Clock::time_point::max()) {
      uv_timer_stop(&clock);
      return;
    }
    const auto now = Clock::now();
    const auto delay = nearest <= now
                           ? std::chrono::milliseconds(1)
                           : std::max(std::chrono::milliseconds(1),
                                      std::chrono::ceil<std::chrono::milliseconds>(
                                          nearest - now));
    uv_timer_start(&clock, onTick, static_cast<std::uint64_t>(delay.count()),
                   0);
  }

  void beginConnect(std::shared_ptr<TcpOperation::Impl> operation,
                    std::string host, std::uint16_t port, int timeoutMs) {
    if (operation->ready())
      return;
    auto attempt = std::make_shared<Attempt>();
    attempt->operation = std::move(operation);
    attempt->host = std::move(host);
    attempt->service = std::to_string(port);
    attempt->deadline = deadlineFor(timeoutMs);
    attempt->resolver.data = attempt.get();
    attempts.emplace(attempt.get(), attempt);
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    const int status =
        uv_getaddrinfo(&loop, &attempt->resolver, onResolved,
                       attempt->host.c_str(), attempt->service.c_str(), &hints);
    if (status < 0) {
      attempt->operation->settle(failure("dns", "TCP name resolution failed"));
      attempts.erase(attempt.get());
    } else {
      attempt->resolving = true;
    }
  }

  static void onResolved(uv_getaddrinfo_t *request, int status,
                         addrinfo *addresses) {
    auto *attempt = static_cast<Attempt *>(request->data);
    auto *self = static_cast<TcpReactor *>(request->loop->data);
    // loop.data is installed before the worker starts.
    attempt->resolving = false;
    if (attempt->operation->ready()) {
      if (addresses)
        uv_freeaddrinfo(addresses);
      self->attempts.erase(attempt);
      self->rearmDeadline();
      return;
    }
    if (status < 0 || !addresses) {
      attempt->operation->settle(failure("dns", "TCP name resolution failed"));
      if (addresses)
        uv_freeaddrinfo(addresses);
      self->attempts.erase(attempt);
      self->rearmDeadline();
      return;
    }
    attempt->addresses = addresses;
    attempt->nextAddress = addresses;
    if (!self->startNextAddress(*attempt)) {
      attempt->operation->settle(failure("connect", "TCP connection failed"));
      self->attempts.erase(attempt);
    }
    self->rearmDeadline();
  }

  bool startNextAddress(Attempt &attempt) {
    // A connection refusal is asynchronous, so retain the remaining addresses.
    while (attempt.nextAddress) {
      auto *address = attempt.nextAddress;
      attempt.nextAddress = address->ai_next;
      auto *handle = new TcpHandle;
      if (uv_tcp_init(&loop, &handle->socket) < 0) {
        delete handle;
        continue;
      }
      handle->socket.data = handle;
      attempt.connector.data = &attempt;
      const int result = uv_tcp_connect(&attempt.connector, &handle->socket,
                                        address->ai_addr, onConnected);
      if (result == 0) {
        attempt.handle = handle;
        attempt.connecting = true;
        return true;
      }
      uv_close(reinterpret_cast<uv_handle_t *>(&handle->socket),
               onSocketClosed);
    }
    return false;
  }

  static void onConnected(uv_connect_t *request, int status) {
    auto *attempt = static_cast<Attempt *>(request->data);
    auto *self = static_cast<TcpReactor *>(request->handle->loop->data);
    auto keepAlive = self->attempts.at(attempt);
    attempt->connecting = false;
    if (!attempt->operation->ready() && Clock::now() >= attempt->deadline)
      attempt->operation->settle(failure("timeout", "TCP connect timed out"));
    if (status < 0 || attempt->operation->ready()) {
      self->closeHandle(attempt->handle);
      attempt->handle = nullptr;
      if (!attempt->operation->ready() && self->startNextAddress(*attempt)) {
        self->rearmDeadline();
        return;
      }
      if (!attempt->operation->ready())
        attempt->operation->settle(failure("connect", "TCP connection failed"));
      self->attempts.erase(attempt);
      self->rearmDeadline();
      return;
    }
    auto state = std::make_shared<TcpConnection::State>();
    state->reactor = self->shared_from_this();
    state->handle = attempt->handle;
    state->isOpen.store(true);
    attempt->handle->state = state;
    self->connections.emplace(state.get(), state);
    if (uv_read_start(reinterpret_cast<uv_stream_t *>(&attempt->handle->socket),
                      onAllocate, onRead) < 0) {
      self->closeConnection(state, "transport", "TCP read could not start");
      attempt->operation->settle(
          failure("transport", "TCP read could not start"));
    } else {
      TcpResponse response;
      response.ok = true;
      response.connection =
          std::shared_ptr<TcpConnection>(new TcpConnection(state));
      attempt->operation->settle(std::move(response));
    }
    self->attempts.erase(attempt);
    self->rearmDeadline();
  }

  static void onAllocate(uv_handle_t *handle, size_t suggested,
                         uv_buf_t *buffer) {
    auto *socket = static_cast<TcpHandle *>(handle->data);
    auto *self = static_cast<TcpReactor *>(handle->loop->data);
    const auto available =
        self->maxBufferedBytes - socket->state->buffered.size();
    const auto count = std::max<std::size_t>(1, std::min(suggested, available));
    buffer->base = new char[count];
    buffer->len = count;
  }

  static void onRead(uv_stream_t *stream, ssize_t received,
                     const uv_buf_t *buffer) {
    auto *socket = static_cast<TcpHandle *>(stream->data);
    auto *self = static_cast<TcpReactor *>(stream->loop->data);
    auto state = socket->state;
    const bool hadPendingRead = state->pendingRead != nullptr;
    if (received > 0) {
      state->buffered.append(buffer->base, static_cast<std::size_t>(received));
      if (state->buffered.size() == self->maxBufferedBytes)
        uv_read_stop(stream);
      self->deliverRead(state);
    } else if (received < 0) {
      state->eof = received == UV_EOF;
      self->closeConnection(state, received == UV_EOF ? "eof" : "transport",
                            received == UV_EOF ? "TCP peer closed"
                                               : "TCP read failed");
    }
    delete[] buffer->base;
    if (hadPendingRead)
      self->rearmDeadline();
  }

  void deliverRead(const std::shared_ptr<TcpConnection::State> &state) {
    auto operation = state->pendingRead;
    if (!operation)
      return;
    if (operation->ready()) {
      state->pendingRead.reset();
      return;
    }
    if (Clock::now() >= state->readDeadline) {
      state->pendingRead.reset();
      operation->settle(failure("timeout", "TCP read timed out"));
      return;
    }
    if (!state->buffered.empty()) {
      TcpResponse response;
      response.ok = true;
      const auto count =
          std::min(state->pendingReadMax, state->buffered.size());
      response.data = state->buffered.substr(0, count);
      state->buffered.erase(0, count);
      state->pendingRead.reset();
      operation->settle(std::move(response));
      if (state->isOpen && state->buffered.size() < maxBufferedBytes)
        uv_read_start(reinterpret_cast<uv_stream_t *>(&state->handle->socket),
                      onAllocate, onRead);
    } else if (!state->isOpen) {
      state->pendingRead.reset();
      operation->settle(
          failure(state->eof ? "eof" : "closed",
                  state->eof ? "TCP peer closed" : "TCP connection closed"));
    }
  }

  void beginRead(const std::shared_ptr<TcpConnection::State> &state,
                 const std::shared_ptr<TcpOperation::Impl> &operation,
                 std::size_t maxBytes, int timeoutMs) {
    if (operation->ready())
      return;
    if (state->pendingRead && !state->pendingRead->ready()) {
      operation->settle(
          failure("read_pending", "TCP connection already has a pending read"));
      return;
    }
    state->pendingRead = operation;
    state->pendingReadMax = maxBytes;
    state->readDeadline = deadlineFor(timeoutMs);
    deliverRead(state);
  }

  void beginWrite(const std::shared_ptr<TcpConnection::State> &state,
                  const std::shared_ptr<TcpOperation::Impl> &operation,
                  std::string data, int timeoutMs) {
    if (operation->ready())
      return;
    if (!state->isOpen) {
      operation->settle(failure("closed", "TCP connection closed"));
      return;
    }
    if (data.empty()) {
      TcpResponse response;
      response.ok = true;
      operation->settle(std::move(response));
      return;
    }
    auto write = std::make_unique<Write>();
    write->connection = state;
    write->operation = operation;
    write->bytes = std::move(data);
    write->deadline = deadlineFor(timeoutMs);
    write->request.data = write.get();
    auto *raw = write.get();
    writes.emplace(raw, std::move(write));
    uv_buf_t buffer = uv_buf_init(raw->bytes.data(),
                                  static_cast<unsigned int>(raw->bytes.size()));
    const int status = uv_write(
        &raw->request, reinterpret_cast<uv_stream_t *>(&state->handle->socket),
        &buffer, 1, onWritten);
    if (status < 0) {
      operation->settle(failure("transport", "TCP write failed"));
      writes.erase(raw);
    }
  }

  static void onWritten(uv_write_t *request, int status) {
    auto *write = static_cast<Write *>(request->data);
    auto *self = static_cast<TcpReactor *>(request->handle->loop->data);
    if (!write->operation->ready() && Clock::now() >= write->deadline) {
      write->operation->settle(failure("timeout", "TCP write timed out"));
      self->closeConnection(write->connection, "closed",
                            "TCP connection closed");
    } else if (status < 0) {
      write->operation->settle(failure("transport", "TCP write failed"));
    } else {
      TcpResponse response;
      response.ok = true;
      write->operation->settle(std::move(response));
    }
    self->writes.erase(write);
    self->rearmDeadline();
  }

  void closeHandle(TcpHandle *handle) {
    if (handle &&
        !uv_is_closing(reinterpret_cast<uv_handle_t *>(&handle->socket)))
      uv_close(reinterpret_cast<uv_handle_t *>(&handle->socket),
               onSocketClosed);
  }

  static void onSocketClosed(uv_handle_t *handle) {
    delete static_cast<TcpHandle *>(handle->data);
  }

  void closeConnection(const std::shared_ptr<TcpConnection::State> &state,
                       const char *code, const char *message) {
    state->isOpen.store(false);
    if (state->handle) {
      uv_read_stop(reinterpret_cast<uv_stream_t *>(&state->handle->socket));
      closeHandle(state->handle);
      state->handle = nullptr;
    }
    if (state->pendingRead) {
      if (state->eof && !state->buffered.empty())
        deliverRead(state);
      else {
        state->pendingRead->settle(failure(code, message));
        state->pendingRead.reset();
      }
    }
    connections.erase(state.get());
  }

  void sweep() {
    const auto now = Clock::now();
    for (auto &[_, attempt] : attempts) {
      if (!attempt->operation->ready() && now >= attempt->deadline)
        attempt->operation->settle(failure("timeout", "TCP connect timed out"));
      if (attempt->operation->ready()) {
        if (attempt->resolving)
          uv_cancel(reinterpret_cast<uv_req_t *>(&attempt->resolver));
        if (attempt->connecting)
          closeHandle(attempt->handle);
      }
    }
    for (auto &[_, state] : connections) {
      if (!state->pendingRead)
        continue;
      if (!state->pendingRead->ready() && now >= state->readDeadline)
        state->pendingRead->settle(failure("timeout", "TCP read timed out"));
      if (state->pendingRead->ready())
        state->pendingRead.reset();
    }
    for (auto &[_, write] : writes) {
      if (!write->operation->ready() && now >= write->deadline) {
        write->operation->settle(failure("timeout", "TCP write timed out"));
        closeConnection(write->connection, "closed", "TCP connection closed");
      }
    }
  }

  void stopOnLoop() {
    for (auto &[_, attempt] : attempts) {
      attempt->operation->settle(failure("shutdown", "TCP client shut down"));
      if (attempt->resolving)
        uv_cancel(reinterpret_cast<uv_req_t *>(&attempt->resolver));
      if (attempt->connecting)
        closeHandle(attempt->handle);
    }
    for (auto &[_, write] : writes)
      write->operation->settle(failure("shutdown", "TCP client shut down"));
    while (!connections.empty())
      closeConnection(connections.begin()->second, "shutdown",
                      "TCP client shut down");
    uv_timer_stop(&clock);
    uv_close(reinterpret_cast<uv_handle_t *>(&clock), nullptr);
    uv_close(reinterpret_cast<uv_handle_t *>(&wakeup), nullptr);
  }

  uv_loop_t loop{};
  uv_async_t wakeup{};
  uv_timer_t clock{};
  std::size_t maxBufferedBytes;
  std::mutex mutex;
  std::mutex shutdownMutex;
  bool finished = false;
  std::deque<std::function<void()>> commands;
  std::atomic<bool> stopping = false;
  std::thread worker;
  std::unordered_map<Attempt *, std::shared_ptr<Attempt>> attempts;
  std::unordered_map<TcpConnection::State *,
                     std::shared_ptr<TcpConnection::State>>
      connections;
  std::unordered_map<Write *, std::unique_ptr<Write>> writes;
};

TcpConnection::TcpConnection(std::shared_ptr<State> state)
    : state_(std::move(state)) {}
TcpConnection::~TcpConnection() { close(); }

std::shared_ptr<TcpOperation> TcpConnection::read(std::size_t maxBytes,
                                                  int timeoutMs) {
  auto reactor = state_->reactor.lock();
  auto operation = reactor ? reactor->makeOperation()
                           : std::make_shared<TcpOperation::Impl>();
  auto result = std::shared_ptr<TcpOperation>(new TcpOperation(operation));
  if (maxBytes == 0 || timeoutMs < 0) {
    operation->settle(
        failure("invalid_argument", "Invalid TCP read arguments"));
  } else if (!reactor || !reactor->post([reactor, state = state_, operation,
                                         maxBytes, timeoutMs] {
               reactor->beginRead(state, operation, maxBytes, timeoutMs);
             })) {
    operation->settle(failure("shutdown", "TCP client shut down"));
  }
  return result;
}

std::shared_ptr<TcpOperation> TcpConnection::write(std::string data,
                                                   int timeoutMs) {
  auto reactor = state_->reactor.lock();
  auto operation = reactor ? reactor->makeOperation()
                           : std::make_shared<TcpOperation::Impl>();
  auto result = std::shared_ptr<TcpOperation>(new TcpOperation(operation));
  if (timeoutMs < 0 || data.size() > static_cast<std::size_t>(UINT_MAX)) {
    operation->settle(
        failure("invalid_argument", "Invalid TCP write arguments"));
  } else if (!reactor ||
             !reactor->post([reactor, state = state_, operation,
                             data = std::move(data), timeoutMs]() mutable {
               reactor->beginWrite(state, operation, std::move(data),
                                   timeoutMs);
             })) {
    operation->settle(failure("shutdown", "TCP client shut down"));
  }
  return result;
}

void TcpConnection::close() {
  state_->isOpen.store(false);
  if (auto reactor = state_->reactor.lock()) {
    reactor->post([reactor, state = state_] {
      // Explicit close also discards any unread bytes.
      state->buffered.clear();
      reactor->closeConnection(state, "closed", "TCP connection closed");
    });
  }
}

bool TcpConnection::open() const { return state_->isOpen.load(); }

struct TcpClient::Impl {
  explicit Impl(Options options) : options(options) {}
  Options options;
  std::mutex mutex;
  std::mutex shutdownMutex;
  bool stopped = false;
  std::shared_ptr<TcpReactor> reactor;
};

TcpClient::TcpClient() : TcpClient(Options{}) {}
TcpClient::TcpClient(Options options)
    : impl_(std::make_unique<Impl>(options)) {}
TcpClient::~TcpClient() { shutdown(); }

std::shared_ptr<TcpOperation>
TcpClient::connect(std::string host, std::uint16_t port, int timeoutMs) {
  std::lock_guard lock(impl_->mutex);
  if (host.empty() || port == 0 || timeoutMs < 0 ||
      impl_->options.maxBufferedBytes == 0) {
    auto operation = std::make_shared<TcpOperation::Impl>();
    operation->settle(
        failure("invalid_argument", "Invalid TCP connect arguments"));
    return std::shared_ptr<TcpOperation>(new TcpOperation(operation));
  }
  if (!impl_->stopped && !impl_->reactor) {
    try {
      impl_->reactor =
          std::make_shared<TcpReactor>(impl_->options.maxBufferedBytes);
      impl_->reactor->loop.data = impl_->reactor.get();
      impl_->reactor->start();
    } catch (...) {
      impl_->stopped = true;
      impl_->reactor.reset();
    }
  }
  auto reactor = impl_->reactor;
  auto operation = reactor ? reactor->makeOperation()
                           : std::make_shared<TcpOperation::Impl>();
  auto result = std::shared_ptr<TcpOperation>(new TcpOperation(operation));
  if (!reactor || !reactor->post([reactor, operation, host = std::move(host),
                                  port, timeoutMs]() mutable {
        reactor->beginConnect(operation, std::move(host), port, timeoutMs);
      })) {
    operation->settle(failure("shutdown", "TCP client unavailable"));
  }
  return result;
}

void TcpClient::shutdown() {
  std::lock_guard shutdownLock(impl_->shutdownMutex);
  std::shared_ptr<TcpReactor> reactor;
  {
    std::lock_guard lock(impl_->mutex);
    impl_->stopped = true;
    reactor = std::move(impl_->reactor);
  }
  if (reactor)
    reactor->shutdown();
}

} // namespace demi::runtime
