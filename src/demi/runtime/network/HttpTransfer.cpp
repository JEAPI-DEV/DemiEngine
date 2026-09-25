#include "demi/runtime/network/HttpTransfer.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>

#if defined(__ANDROID__)
#include <unistd.h>
#endif

namespace demi::runtime::http {
namespace {

bool token(const std::string_view value) {
  return !value.empty() && std::ranges::all_of(value, [](const unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(c)) != std::string_view::npos;
  });
}

bool invalidHeaderValue(const std::string_view value) {
  return std::ranges::any_of(value, [](const unsigned char c) {
    return (c < 32 && c != '\t') || c == 127;
  });
}

HttpResponse curlFailure(const CURLcode result) {
  // Never expose curl's error buffer, request strings, or backend diagnostics.
  switch (result) {
  case CURLE_OPERATION_TIMEDOUT:
    return failure("timeout", "HTTP request timed out");
  case CURLE_COULDNT_RESOLVE_HOST:
  case CURLE_COULDNT_RESOLVE_PROXY:
    return failure("dns", "HTTP name resolution failed");
  case CURLE_COULDNT_CONNECT:
    return failure("connect", "HTTP connection failed");
  case CURLE_PEER_FAILED_VERIFICATION:
  case CURLE_SSL_CONNECT_ERROR:
  case CURLE_SSL_CERTPROBLEM:
  case CURLE_SSL_CIPHER:
  case CURLE_SSL_CACERT_BADFILE:
  case CURLE_SSL_CRL_BADFILE:
  case CURLE_SSL_ISSUER_ERROR:
  case CURLE_SSL_PINNEDPUBKEYNOTMATCH:
  case CURLE_SSL_INVALIDCERTSTATUS:
    return failure("tls", "HTTP TLS verification or handshake failed");
  case CURLE_OUT_OF_MEMORY:
  case CURLE_FAILED_INIT:
  case CURLE_UNKNOWN_OPTION:
  case CURLE_NOT_BUILT_IN:
    return failure("internal", "HTTP transport initialization failed");
  default:
    return failure("transport", "HTTP transport failed");
  }
}

} // namespace

CurlRuntime::CurlRuntime() {
  initialized_ = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
  if (!initialized_) {
    error_ = "HTTP transport initialization failed";
    return;
  }
  const auto* version = curl_version_info(CURLVERSION_NOW);
  if (!version || version->version_num < 0x075500 || !(version->features & CURL_VERSION_SSL) ||
      !(version->features & CURL_VERSION_THREADSAFE)) {
    error_ = "HTTP requires libcurl 7.85 or newer with TLS and thread-safe initialization";
  } else if (!(version->features & CURL_VERSION_ASYNCHDNS)) {
    // Both c-ares and curl's threaded system resolver keep DNS work off the
    // caller. Threaded resolver cleanup can still wait for an OS lookup.
    error_ = "HTTP requires libcurl with asynchronous DNS resolution";
  }
}

CurlRuntime::~CurlRuntime() {
  if (initialized_) {
    curl_global_cleanup();
  }
}

std::shared_ptr<const CurlRuntime> CurlRuntime::acquire() {
  // Client-held references also protect static clients during process teardown.
  static const std::shared_ptr<const CurlRuntime> runtime(new CurlRuntime);
  return runtime;
}

bool validate(const HttpRequest& request, std::string& error) {
  if (request.timeoutMs < 0 || request.connectTimeoutMs < 0) {
    error = "HTTP timeouts must be nonnegative";
    return false;
  }
  if (!token(request.method)) {
    error = "HTTP method must be a nonempty HTTP token";
    return false;
  }
  for (const auto& [name, value] : request.headers) {
    if (!token(name) || invalidHeaderValue(value)) {
      error = "HTTP headers require token names and values without control characters";
      return false;
    }
  }
  if (request.body.size() > static_cast<std::uintmax_t>(std::numeric_limits<curl_off_t>::max())) {
    error = "HTTP request body cannot be represented by the transport";
    return false;
  }
  if (request.caCertificatePem.find('\0') != std::string::npos) {
    error = "HTTP CA PEM must not contain NUL bytes";
    return false;
  }
  if (request.url.empty() || request.url.find("://") == std::string::npos ||
      std::ranges::any_of(request.url, [](const unsigned char c) {
        return c <= 32 || c == 127;
      })) {
    error = "HTTP URL must be an absolute HTTP or HTTPS URL without whitespace or control characters";
    return false;
  }
  const std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> url(curl_url(), curl_url_cleanup);
  if (!url) {
    error = "HTTP URL validation could not allocate storage";
    return false;
  }
  // DISALLOW_USER also rejects empty userinfo, password-only and encoded users.
  if (curl_url_set(url.get(), CURLUPART_URL, request.url.c_str(), CURLU_DISALLOW_USER) != CURLUE_OK) {
    error = "HTTP URL is invalid or contains embedded credentials";
    return false;
  }
  char* scheme = nullptr;
  const auto result = curl_url_get(url.get(), CURLUPART_SCHEME, &scheme, 0);
  const bool supported =
      result == CURLUE_OK && scheme && (std::string_view(scheme) == "http" || std::string_view(scheme) == "https");
  curl_free(scheme);
  if (!supported) {
    error = "HTTP URL scheme must be HTTP or HTTPS";
  }
  return supported;
}

HttpResponse failure(const char* code, const char* message) {
  HttpResponse response;
  response.errorCode = code;
  response.error = message;
  return response;
}

Transfer::Transfer(HttpRequest request, const std::atomic<bool>& cancelled, const std::atomic<bool>& stopping)
    : request_(std::move(request)), cancelled_(cancelled), stopping_(stopping), easy_(curl_easy_init()) {}

Transfer::~Transfer() {
  if (easy_) {
    curl_easy_cleanup(easy_);
  }
  curl_slist_free_all(headers_);
}

CURLcode Transfer::configure() {
  if (!easy_) {
    return CURLE_OUT_OF_MEMORY;
  }
  CURLcode result = CURLE_OK;
  const auto set = [this, &result](const CURLoption option, auto value) {
    if (result == CURLE_OK) {
      result = curl_easy_setopt(easy_, option, value);
    }
  };
  set(CURLOPT_URL, request_.url.c_str());
  set(CURLOPT_PROTOCOLS_STR, "http,https");
  set(CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
  set(CURLOPT_DISALLOW_USERNAME_IN_URL, 1L);
  set(CURLOPT_FOLLOWLOCATION, 0L);
  set(CURLOPT_FAILONERROR, 0L);
  // Advertise this build's supported encodings and decode before writeBody,
  // where the response limit therefore applies to decompressed bytes. An
  // explicit Accept-Encoding header overrides negotiation, not decoder support.
  set(CURLOPT_ACCEPT_ENCODING, "");
  set(CURLOPT_NOSIGNAL, 1L);
  set(CURLOPT_TIMEOUT_MS, static_cast<long>(request_.timeoutMs));
  set(CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(request_.connectTimeoutMs));
  set(CURLOPT_SSL_VERIFYPEER, 1L);
  set(CURLOPT_SSL_VERIFYHOST, 2L);
  // Retain curl's standard proxy environment behavior. Fresh easy handles
  // ignore netrc by default, including builds without netrc support.
  set(CURLOPT_WRITEFUNCTION, &Transfer::writeBody);
  set(CURLOPT_WRITEDATA, this);
  set(CURLOPT_HEADERFUNCTION, &Transfer::writeHeader);
  set(CURLOPT_HEADERDATA, this);
  set(CURLOPT_XFERINFOFUNCTION, &Transfer::progress);
  set(CURLOPT_XFERINFODATA, this);
  set(CURLOPT_NOPROGRESS, 0L);

  if (!request_.caCertificatePem.empty()) {
    // Clear both default trust sources so supplied trust is authoritative.
    set(CURLOPT_CAINFO, static_cast<const char*>(nullptr));
    set(CURLOPT_CAPATH, static_cast<const char*>(nullptr));
    curl_blob pem{request_.caCertificatePem.data(), request_.caCertificatePem.size(), CURL_BLOB_COPY};
    set(CURLOPT_CAINFO_BLOB, &pem);
  }
#if defined(__ANDROID__)
  else {
    // The Android curl/mbedTLS build supports x509_crt_parse_path. Use live OS
    // roots, preferring the updatable Conscrypt store (Android 14+). Android
    // app network-security-config/user-installed roots are not a native CA API.
    const char* roots = "/apex/com.android.conscrypt/cacerts";
    if (access(roots, R_OK | X_OK) != 0) {
      roots = "/system/etc/security/cacerts";
    }
    set(CURLOPT_CAINFO, static_cast<const char*>(nullptr));
    set(CURLOPT_CAPATH, roots);
  }
#endif

  for (const auto& [name, value] : request_.headers) {
    // curl's semicolon spelling sends an empty header instead of deleting it.
    const std::string header = name + (value.empty() ? ";" : ": " + value);
    auto* appended = curl_slist_append(headers_, header.c_str());
    if (!appended) {
      return CURLE_OUT_OF_MEMORY;
    }
    headers_ = appended;
  }
  set(CURLOPT_HTTPHEADER, headers_);
  if (!request_.body.empty() || request_.method == "POST" || request_.method == "PUT" || request_.method == "PATCH") {
    set(CURLOPT_POSTFIELDSIZE_LARGE, static_cast<curl_off_t>(request_.body.size()));
    set(CURLOPT_POSTFIELDS, request_.body.data());
  }
  if (request_.method == "HEAD") {
    set(CURLOPT_NOBODY, 1L);
  }
  set(CURLOPT_CUSTOMREQUEST, request_.method.c_str());
  return result;
}

bool Transfer::cancelled() const {
  return cancelled_.load(std::memory_order_relaxed) || stopping_.load(std::memory_order_relaxed);
}

int Transfer::progress(void* user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
  return static_cast<Transfer*>(user)->cancelled() ? 1 : 0;
}

std::size_t Transfer::writeBody(char* bytes, const std::size_t size, const std::size_t count, void* user) noexcept {
  auto& self = *static_cast<Transfer*>(user);
  if (self.cancelled()) {
    return 0;
  }
  if (size && count > std::numeric_limits<std::size_t>::max() / size) {
    self.callbackFailed_ = true;
    return 0;
  }
  const auto length = size * count;
  const auto limit = self.request_.maxResponseBytes;
  if (limit && length > limit - self.response_.body.size()) {
    self.exceededLimit_ = true;
    return 0;
  }
  try {
    self.response_.body.append(bytes, length);
    return length;
  } catch (...) {
    self.callbackFailed_ = true;
    return 0;
  }
}

std::size_t Transfer::writeHeader(char* bytes, const std::size_t size, const std::size_t count, void* user) noexcept {
  auto& self = *static_cast<Transfer*>(user);
  if (self.cancelled()) {
    return 0;
  }
  if (size && count > std::numeric_limits<std::size_t>::max() / size) {
    self.callbackFailed_ = true;
    return 0;
  }
  const auto length = size * count;
  const auto limit = self.request_.maxHeaderBytes;
  if (limit && length > limit - self.headerBytes_) {
    self.exceededHeaderLimit_ = true;
    return 0;
  }
  // Count all blocks cumulatively, even when an interim response is discarded.
  // An unlimited request does not need a counter and cannot overflow one.
  if (limit) {
    self.headerBytes_ += length;
  }
  try {
    std::string_view line(bytes, length);
    if (line.starts_with("HTTP/")) {
      // Discard interim 1xx header blocks; retain final headers and trailers.
      self.response_.headers.clear();
    } else if (const auto colon = line.find(':'); colon != std::string_view::npos) {
      auto value = line.substr(colon + 1);
      while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
      }
      while (!value.empty() &&
             (value.back() == '\r' || value.back() == '\n' || value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
      }
      self.response_.headers.emplace_back(line.substr(0, colon), value);
    }
    return length;
  } catch (...) {
    self.callbackFailed_ = true;
    return 0;
  }
}

HttpResponse Transfer::finish(const CURLcode result) {
  if (cancelled()) {
    return failure("cancelled", "HTTP request cancelled");
  }
  if (exceededLimit_) {
    return failure("response_too_large", "HTTP response exceeded the configured body limit");
  }
  if (exceededHeaderLimit_) {
    return failure("response_too_large", "HTTP response exceeded the configured header limit");
  }
  if (callbackFailed_) {
    return failure("internal", "HTTP response storage failed");
  }
  if (result != CURLE_OK) {
    return curlFailure(result);
  }
  long status = 0;
  if (curl_easy_getinfo(easy_, CURLINFO_RESPONSE_CODE, &status) != CURLE_OK || status == 0) {
    return failure("transport", "HTTP response status unavailable");
  }
  response_.status = static_cast<int>(status);
  response_.ok = status >= 200 && status < 300;
  return std::move(response_);
}

} // namespace demi::runtime::http
