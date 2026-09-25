#pragma once

// Private curl adapter. Gameplay and bindings include HttpClient.h instead.
#include "demi/runtime/network/HttpClient.h"

#include <atomic>
#include <curl/curl.h>

namespace demi::runtime::http {

class CurlRuntime {
public:
  static std::shared_ptr<const CurlRuntime> acquire();
  ~CurlRuntime();
  [[nodiscard]] const std::string& error() const {
    return error_;
  }

private:
  CurlRuntime();
  bool initialized_ = false;
  std::string error_;
};

[[nodiscard]] bool validate(const HttpRequest& request, std::string& error);
[[nodiscard]] HttpResponse failure(const char* code, const char* message);

// Owns a single easy handle, its upload storage, and response callbacks.
// All access except the cancellation flag belongs to the client's worker.
class Transfer {
public:
  Transfer(HttpRequest request, const std::atomic<bool>& cancelled, const std::atomic<bool>& stopping);
  ~Transfer();
  Transfer(const Transfer&) = delete;
  Transfer& operator=(const Transfer&) = delete;

  [[nodiscard]] CURL* handle() const {
    return easy_;
  }
  [[nodiscard]] CURLcode configure();
  [[nodiscard]] HttpResponse finish(CURLcode result);

private:
  static std::size_t writeBody(char* bytes, std::size_t size, std::size_t count, void* user) noexcept;
  static std::size_t writeHeader(char* bytes, std::size_t size, std::size_t count, void* user) noexcept;
  static int progress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept;
  [[nodiscard]] bool cancelled() const;

  HttpRequest request_;
  const std::atomic<bool>& cancelled_;
  const std::atomic<bool>& stopping_;
  CURL* easy_ = nullptr;
  curl_slist* headers_ = nullptr;
  HttpResponse response_;
  std::size_t headerBytes_ = 0;
  bool exceededLimit_ = false;
  bool exceededHeaderLimit_ = false;
  bool callbackFailed_ = false;
};

} // namespace demi::runtime::http
