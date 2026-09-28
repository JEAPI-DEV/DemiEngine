#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace demi::runtime {

class AsyncCompletion;
class TcpConnection;
struct TcpReactor;

struct TcpResponse {
  bool ok = false;
  std::string data;
  std::string errorCode;
  std::string error;
  std::shared_ptr<TcpConnection> connection;
};

class TcpOperation {
public:
  struct Impl;

  [[nodiscard]] std::shared_ptr<AsyncCompletion> completion() const;
  // Pending returns nullopt. Results are stable, independent copies.
  [[nodiscard]] std::optional<TcpResponse> response() const;
  // Logical settlement is immediate. Native cancellation/cleanup runs on the
  // reactor; an already submitted write may have reached the peer.
  void cancel();

private:
  explicit TcpOperation(std::shared_ptr<Impl> impl);
  std::shared_ptr<Impl> impl_;
  friend class TcpClient;
  friend class TcpConnection;
};

class TcpConnection {
public:
  struct State;
  ~TcpConnection();

  // TCP is an unframed byte stream. A successful read returns whatever is
  // available, up to maxBytes. Only one read may be pending per connection.
  // Timeout/cancellation preserves buffered bytes for the next read.
  [[nodiscard]] std::shared_ptr<TcpOperation> read(std::size_t maxBytes,
                                                   int timeoutMs);
  // A timed-out write closes the connection; submitted bytes cannot be
  // retracted or identified as delivered.
  [[nodiscard]] std::shared_ptr<TcpOperation> write(std::string data,
                                                    int timeoutMs);
  void close();
  [[nodiscard]] bool open() const;

private:
  explicit TcpConnection(std::shared_ptr<State> state);
  std::shared_ptr<State> state_;
  friend class TcpClient;
  friend struct TcpOperation::Impl;
  friend struct TcpReactor;
};

class TcpClient {
public:
  struct Options {
    // Backpressure stops libuv reads at this limit. Never truncates bytes.
    std::size_t maxBufferedBytes = 1024 * 1024;
  };

  TcpClient();
  explicit TcpClient(Options options);
  ~TcpClient();
  TcpClient(const TcpClient &) = delete;
  TcpClient &operator=(const TcpClient &) = delete;

  // Timeouts are nonnegative milliseconds; zero disables the deadline.
  [[nodiscard]] std::shared_ptr<TcpOperation>
  connect(std::string host, std::uint16_t port, int timeoutMs);
  // Terminal, idempotent and joins the reactor. OS DNS cancellation is best
  // effort, so shutdown can wait for a system resolver call to return.
  void shutdown();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace demi::runtime
