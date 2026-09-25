#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace demi::runtime {

struct HttpRequest {
  std::string url;
  std::string method = "GET";
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
  int timeoutMs = 30000;
  int connectTimeoutMs = 5000;
  // Maximum delivered body bytes; zero means unlimited.
  std::size_t maxResponseBytes = 16 * 1024 * 1024;
  // Cumulative received header bytes, including status lines, interim blocks,
  // and trailers. Zero means unlimited.
  std::size_t maxHeaderBytes = 64 * 1024;
  // Empty uses platform trust. Explicit PEM replaces the default trust store.
  std::string caCertificatePem;
};

struct HttpResponse {
  bool ok = false;
  int status = 0;
  std::string body;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string error;
  // Empty for HTTP responses; otherwise cancelled, timeout, response_too_large,
  // dns, connect, tls, transport, or internal. Errors never echo request data.
  std::string errorCode;
};

class HttpOperation {
public:
  ~HttpOperation();
  HttpOperation(const HttpOperation&) = delete;
  HttpOperation& operator=(const HttpOperation&) = delete;

  [[nodiscard]] bool done() const;
  // Pending returns nullopt; completed responses are stable, independent copies.
  [[nodiscard]] std::optional<HttpResponse> response() const;
  // Idempotent. Pending work receives a cancelled response immediately; a
  // completed operation is unchanged. Transport cleanup happens on the worker.
  void cancel();

private:
  struct Impl;
  explicit HttpOperation(std::shared_ptr<Impl> impl);
  std::shared_ptr<Impl> impl_;
  friend class HttpClient;
};

class HttpClient {
public:
  HttpClient();
  ~HttpClient();
  HttpClient(const HttpClient&) = delete;
  HttpClient& operator=(const HttpClient&) = delete;

  // Validates without network I/O. Invalid/unavailable requests return nullptr
  // and a sanitized diagnostic. A successful submission clears error.
  // HTTP/HTTPS only; redirects are returned, never automatically followed:
  // callers must decide whether custom credentials may be sent to another URL.
  // HTTP errors (including 3xx/4xx/5xx) have empty error/errorCode; only 2xx is ok.
  // Timeouts must be nonnegative: zero uses curl's unlimited overall timeout or
  // default connection timeout. No application-imposed request-count limit.
  [[nodiscard]] std::shared_ptr<HttpOperation> request(HttpRequest request, std::string& error);

  // Terminal and idempotent. Wakes/joins the worker and cancels all pending
  // handles, which remain safe to inspect after client destruction.
  // With curl's threaded resolver, cleanup may wait for an outstanding system
  // DNS lookup. Shutdown/destruction has no hard upper bound if that OS call
  // stalls; the worker is never detached or forcibly cancelled.
  void shutdown();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace demi::runtime
