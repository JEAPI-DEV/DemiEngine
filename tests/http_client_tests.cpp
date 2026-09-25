#include "demi/runtime/network/HttpClient.h"
#include "demi/runtime/scripting/LuaScriptHost.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>
#include <nlohmann/json.hpp>

#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>

// The test target supplies the same fixture paths as demi-network-tests:
// DEMI_DTLS_TEST_CERT=server5.crt, DEMI_DTLS_TEST_KEY=server5.key,
// DEMI_DTLS_TEST_CA=test-ca2.crt from mbedTLS/framework/data_files.
namespace {

using demi::runtime::HttpClient;
using demi::runtime::HttpOperation;
using demi::runtime::HttpRequest;
using demi::runtime::HttpResponse;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

void require(const bool condition, const char *message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <class Predicate>
void waitFor(Predicate predicate, const char *message,
             const std::chrono::milliseconds timeout = 3000ms) {
  const auto deadline = Clock::now() + timeout;
  while (!predicate() && Clock::now() < deadline) {
    std::this_thread::sleep_for(1ms);
  }
  require(predicate(), message);
}

std::string readFile(const char *path) {
  std::ifstream file(path, std::ios::binary);
  require(file.good(), "could not read HTTP TLS fixture");
  return {std::istreambuf_iterator<char>(file), {}};
}

class ProxyEnvironmentFixture {
public:
  ProxyEnvironmentFixture() {
    // Set up before any HTTP worker exists, restore after all clients stop.
    for (const auto *name :
         {"http_proxy", "HTTP_PROXY", "https_proxy", "HTTPS_PROXY", "all_proxy",
          "ALL_PROXY", "no_proxy", "NO_PROXY"}) {
      const auto *value = std::getenv(name);
      saved_.emplace_back(name, value ? std::optional<std::string>(value)
                                      : std::nullopt);
    }
    for (const auto &[name, value] : saved_) {
      if (unsetenv(name) != 0) {
        restore();
        throw std::runtime_error(
            "could not isolate HTTP fixture proxy environment");
      }
    }
  }

  ~ProxyEnvironmentFixture() { restore(); }

private:
  void restore() const {
    for (const auto &[name, value] : saved_) {
      if (value) {
        (void)setenv(name, value->c_str(), 1);
      } else {
        (void)unsetenv(name);
      }
    }
  }

  std::vector<std::pair<const char *, std::optional<std::string>>> saved_;
};

struct Socket {
  int fd = -1;
  ~Socket() {
    if (fd >= 0) {
      close(fd);
    }
  }
};

struct TlsConfig {
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context random;
  mbedtls_x509_crt certificate;
  mbedtls_pk_context key;
  mbedtls_ssl_config config;

  TlsConfig() {
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&random);
    mbedtls_x509_crt_init(&certificate);
    mbedtls_pk_init(&key);
    mbedtls_ssl_config_init(&config);
  }
  ~TlsConfig() {
    mbedtls_ssl_config_free(&config);
    mbedtls_pk_free(&key);
    mbedtls_x509_crt_free(&certificate);
    mbedtls_ctr_drbg_free(&random);
    mbedtls_entropy_free(&entropy);
  }
  void configure() {
    constexpr unsigned char Personalization[] = "demi-http-fixture";
    require(mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy,
                                  Personalization,
                                  sizeof(Personalization)) == 0,
            "TLS fixture entropy failed");
    require(mbedtls_x509_crt_parse_file(&certificate, DEMI_DTLS_TEST_CERT) == 0,
            "TLS fixture certificate failed");
    require(mbedtls_pk_parse_keyfile(&key, DEMI_DTLS_TEST_KEY, nullptr,
                                     mbedtls_ctr_drbg_random, &random) == 0,
            "TLS fixture key failed");
    require(mbedtls_ssl_config_defaults(&config, MBEDTLS_SSL_IS_SERVER,
                                        MBEDTLS_SSL_TRANSPORT_STREAM,
                                        MBEDTLS_SSL_PRESET_DEFAULT) == 0,
            "TLS fixture config failed");
    mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &random);
    require(mbedtls_ssl_conf_own_cert(&config, &certificate, &key) == 0,
            "TLS fixture identity failed");
  }
};

struct TlsConnection {
  mbedtls_ssl_context ssl;
  TlsConnection() { mbedtls_ssl_init(&ssl); }
  ~TlsConnection() { mbedtls_ssl_free(&ssl); }
};

// A bounded, loopback-only raw HTTP fixture. TLS uses the repository's mbedTLS
// test identity; no external service, shell process, fixed port or public DNS.
class Fixture {
public:
  Fixture(std::string wireResponse, const bool tls = false,
          const std::chrono::milliseconds delay = 0ms)
      : response_(std::move(wireResponse)), tls_(tls), delay_(delay) {
    if (tls_) {
      tlsConfig_.configure();
    }
    listener_.fd =
        socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    require(listener_.fd >= 0, "HTTP fixture socket failed");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    require(bind(listener_.fd, reinterpret_cast<sockaddr *>(&address),
                 sizeof(address)) == 0,
            "HTTP fixture bind failed");
    socklen_t size = sizeof(address);
    require(getsockname(listener_.fd, reinterpret_cast<sockaddr *>(&address),
                        &size) == 0,
            "HTTP fixture address failed");
    port_ = ntohs(address.sin_port);
    require(listen(listener_.fd, 256) == 0, "HTTP fixture listen failed");
    thread_ = std::thread([this] {
      try {
        run();
      } catch (...) {
        failed_.store(true);
      }
    });
  }
  ~Fixture() {
    stop_.store(true);
    if (thread_.joinable()) {
      thread_.join();
    }
  }
  std::string url(const bool localhost = false) const {
    return std::string(tls_ ? "https://" : "http://") +
           (localhost ? "localhost:" : "127.0.0.1:") + std::to_string(port_) +
           "/probe";
  }
  unsigned requests() const { return requests_.load(); }
  std::string received() const {
    std::lock_guard lock(mutex_);
    return received_;
  }
  void awaitRequest() const {
    waitFor([this] { return requests() != 0 || failed_.load(); },
            "HTTP fixture received no request");
    require(!failed_.load(), "HTTP fixture failed");
  }

private:
  static int tlsSend(void *context, const unsigned char *bytes,
                     const std::size_t size) {
    const auto count =
        send(*static_cast<int *>(context), bytes, size, MSG_NOSIGNAL);
    if (count >= 0) {
      return static_cast<int>(count);
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
  }
  static int tlsReceive(void *context, unsigned char *bytes,
                        const std::size_t size) {
    const auto count = recv(*static_cast<int *>(context), bytes, size, 0);
    if (count >= 0) {
      return static_cast<int>(count);
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
      return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return MBEDTLS_ERR_SSL_INTERNAL_ERROR;
  }
  bool retry(const int result, const int fd,
             const Clock::time_point deadline) const {
    if (stop_.load() || Clock::now() >= deadline) {
      return false;
    }
    if (result != MBEDTLS_ERR_SSL_WANT_READ &&
        result != MBEDTLS_ERR_SSL_WANT_WRITE) {
      return false;
    }
    pollfd item{fd,
                static_cast<short>(
                    result == MBEDTLS_ERR_SSL_WANT_READ ? POLLIN : POLLOUT),
                0};
    (void)poll(&item, 1, 10);
    return true;
  }
  void serve(int fd) {
    const auto deadline = Clock::now() + 5s;
    TlsConnection connection;
    if (tls_) {
      if (mbedtls_ssl_setup(&connection.ssl, &tlsConfig_.config) != 0) {
        return;
      }
      mbedtls_ssl_set_bio(&connection.ssl, &fd, tlsSend, tlsReceive, nullptr);
      int result;
      while ((result = mbedtls_ssl_handshake(&connection.ssl)) != 0) {
        if (!retry(result, fd, deadline)) {
          return;
        }
      }
    }
    std::string request;
    std::array<unsigned char, 4096> buffer;
    std::size_t totalSize = 0;
    while (!stop_.load() && Clock::now() < deadline) {
      const int count =
          tls_ ? mbedtls_ssl_read(&connection.ssl, buffer.data(), buffer.size())
               : tlsReceive(&fd, buffer.data(), buffer.size());
      if (count <= 0) {
        if (!retry(count, fd, deadline)) {
          return;
        }
        continue;
      }
      request.append(reinterpret_cast<const char *>(buffer.data()),
                     static_cast<std::size_t>(count));
      if (totalSize == 0) {
        const auto end = request.find("\r\n\r\n");
        if (end == std::string::npos) {
          continue;
        }
        totalSize = end + 4;
        const auto length = request.find("Content-Length: ");
        if (length != std::string::npos && length < end) {
          std::size_t bodySize = 0;
          const char *begin = request.data() + length +
                              std::string_view("Content-Length: ").size();
          (void)std::from_chars(begin, request.data() + end, bodySize);
          totalSize += bodySize;
        }
      }
      if (request.size() >= totalSize) {
        break;
      }
    }
    if (stop_.load() || request.size() < totalSize || totalSize == 0) {
      return;
    }
    {
      std::lock_guard lock(mutex_);
      received_ = std::move(request);
    }
    requests_.fetch_add(1);
    const auto ready = Clock::now() + delay_;
    while (!stop_.load() && Clock::now() < ready) {
      std::this_thread::sleep_for(1ms);
    }
    std::size_t offset = 0;
    while (!stop_.load() && offset < response_.size() &&
           Clock::now() < deadline) {
      const auto *bytes =
          reinterpret_cast<const unsigned char *>(response_.data() + offset);
      const int count = tls_ ? mbedtls_ssl_write(&connection.ssl, bytes,
                                                 response_.size() - offset)
                             : tlsSend(&fd, bytes, response_.size() - offset);
      if (count > 0) {
        offset += static_cast<std::size_t>(count);
      } else if (!retry(count, fd, deadline)) {
        return;
      }
    }
    if (tls_) {
      (void)mbedtls_ssl_close_notify(&connection.ssl);
    }
  }
  void run() {
    while (!stop_.load()) {
      pollfd item{listener_.fd, POLLIN, 0};
      if (poll(&item, 1, 10) <= 0) {
        continue;
      }
      Socket peer{accept4(listener_.fd, nullptr, nullptr,
                          SOCK_NONBLOCK | SOCK_CLOEXEC)};
      if (peer.fd >= 0) {
        serve(peer.fd);
      }
    }
  }

  std::string response_;
  bool tls_;
  std::chrono::milliseconds delay_;
  TlsConfig tlsConfig_;
  Socket listener_;
  std::uint16_t port_ = 0;
  std::atomic<bool> stop_ = false;
  std::atomic<bool> failed_ = false;
  std::atomic<unsigned> requests_ = 0;
  mutable std::mutex mutex_;
  std::string received_;
  std::thread thread_;
};

std::string wire(const std::string &body,
                 const std::string_view status = "200 OK",
                 const std::string_view headers = "") {
  return "HTTP/1.1 " + std::string(status) +
         "\r\nContent-Length: " + std::to_string(body.size()) +
         "\r\nConnection: close\r\n" + std::string(headers) + "\r\n" + body;
}

std::shared_ptr<HttpOperation> submit(HttpClient &client, HttpRequest request) {
  std::string error = "old diagnostic";
  auto operation = client.request(std::move(request), error);
  if (!operation) {
    throw std::runtime_error("HTTP submission failed: " + error);
  }
  require(error.empty(), "successful HTTP submission retained an error");
  return operation;
}

HttpResponse completed(const std::shared_ptr<HttpOperation> &operation) {
  waitFor([&] { return operation->done(); }, "HTTP operation did not complete");
  const auto response = operation->response();
  require(response.has_value(), "completed HTTP operation has no response");
  return *response;
}

void validation() {
  HttpClient client;
  // A valid loopback transfer first verifies runtime availability; otherwise
  // dependency misconfiguration could masquerade as successful rejection tests.
  Fixture fixture(wire("ok"));
  require(completed(submit(client, {.url = fixture.url()})).ok,
          "HTTP runtime unavailable");
  const std::array invalidUrls = {"file:///etc/passwd",
                                  "ftp://127.0.0.1/",
                                  "localhost/path",
                                  "http://",
                                  "http://user:secret@127.0.0.1/",
                                  "http://@127.0.0.1/",
                                  "http://user%40secret@127.0.0.1/",
                                  "http://127.0.0.1/\r\nsecret"};
  const auto reject = [&](HttpRequest request) {
    std::string error;
    require(!client.request(std::move(request), error),
            "invalid HTTP request was accepted");
    require(!error.empty() && error.find("secret") == std::string::npos,
            "HTTP validation diagnostic is absent or exposes request data");
  };
  for (const auto *url : invalidUrls) {
    reject({.url = url});
  }
  auto request = HttpRequest{.url = fixture.url()};
  request.url += std::string("\0secret", 7);
  reject(request);
  request.url = fixture.url();
  for (const auto &method :
       {std::string(), std::string("GET secret"), std::string("GET\r\nsecret"),
        std::string("GET\0secret", 10)}) {
    request.method = method;
    reject(request);
  }
  request.method = "GET";
  for (const auto &header : std::vector<std::pair<std::string, std::string>>{
           {"", "value"},
           {"Bad Name", "value"},
           {"Name:", "value"},
           {"Name", "value\r\nsecret"},
           {"Name", std::string("value\0secret", 12)},
           {std::string("Name\0secret", 11), "value"}}) {
    request.headers = {header};
    reject(request);
  }
  request.headers.clear();
  request.timeoutMs = -1;
  reject(request);
  request.timeoutMs = 1;
  request.connectTimeoutMs = -1;
  reject(request);
  client.shutdown();
  reject({.url = fixture.url()});
}

void binaryAndHeaders() {
  const std::string binary("a\0b\xffz", 5);
  Fixture fixture(wire(binary, "201 Created",
                       "Set-Cookie: a=1\r\nSet-Cookie: b=2\r\nX-Empty:\r\n"));
  HttpClient client;
  HttpRequest request{
      .url = fixture.url(),
      .method = "PATCH",
      .headers = {{"X-Probe", "one"}, {"X-Probe", "two"}, {"X-Empty", ""}},
      .body = binary};
  const auto operation = submit(client, request);
  auto response = completed(operation);
  require(response.ok && response.status == 201 && response.error.empty() &&
              response.errorCode.empty(),
          "HTTP success status was not preserved");
  require(response.body == binary, "HTTP response was not binary safe");
  std::vector<std::string> cookies;
  for (const auto &[name, value] : response.headers) {
    if (name == "Set-Cookie") {
      cookies.push_back(value);
    }
  }
  require(cookies == std::vector<std::string>{"a=1", "b=2"},
          "duplicate HTTP response headers were lost");
  const auto raw = fixture.received();
  require(raw.starts_with("PATCH /probe HTTP/") &&
              raw.substr(raw.find("\r\n\r\n") + 4) == binary,
          "HTTP method or binary upload changed");
  require(raw.find("X-Probe: one\r\nX-Probe: two\r\n") != std::string::npos &&
              raw.find("X-Empty:\r\n") != std::string::npos,
          "request duplicate or empty headers were lost");
  response.body = "mutated copy";
  operation->cancel();
  require(operation->response()->body == binary,
          "completed HTTP response was mutable or cancellation overwrote it");
}

void statusesAndRedirects() {
  HttpClient client;
  for (const auto status : {"404 Not Found", "503 Unavailable"}) {
    Fixture fixture(wire("server body", status));
    const auto response = completed(submit(client, {.url = fixture.url()}));
    require(!response.ok && response.status >= 400 &&
                response.body == "server body" && response.error.empty() &&
                response.errorCode.empty(),
            "HTTP error status became a transport failure");
  }
  Fixture target(wire("must not be requested"));
  Fixture redirect(
      wire("redirect", "302 Found", "Location: " + target.url() + "\r\n"));
  const auto response = completed(submit(
      client, {.url = redirect.url(), .headers = {{"X-Secret", "private"}}}));
  require(response.status == 302 && !response.ok && response.error.empty() &&
              target.requests() == 0,
          "HTTP redirect forwarded a custom secret");
  Fixture head(
      "HTTP/1.1 200 OK\r\nContent-Length: 123\r\nConnection: close\r\n\r\n");
  const auto headResponse =
      completed(submit(client, {.url = head.url(), .method = "HEAD"}));
  require(headResponse.ok && headResponse.body.empty(),
          "HTTP HEAD waited for a nonexistent body");
}

void chunkedAndLimits() {
  const std::string chunks =
      "HTTP/1.1 103 Early Hints\r\nX-Interim: omitted\r\n\r\n"
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n"
      "X-Final: yes\r\n\r\n3\r\nabc\r\n3\r\ndef\r\n0\r\nX-Trailer: yes\r\n\r\n";
  Fixture fixture(chunks);
  HttpClient client;
  auto response =
      completed(submit(client, {.url = fixture.url(), .maxResponseBytes = 6}));
  require(response.ok && response.body == "abcdef",
          "chunked response or exact body limit failed");
  require(std::ranges::none_of(
              response.headers,
              [](const auto &header) { return header.first == "X-Interim"; }),
          "interim response headers were retained");
  require(std::ranges::any_of(
              response.headers,
              [](const auto &header) { return header.first == "X-Trailer"; }),
          "HTTP trailers were discarded");
  response =
      completed(submit(client, {.url = fixture.url(), .maxResponseBytes = 5}));
  require(!response.ok && response.errorCode == "response_too_large" &&
              response.body.empty(),
          "chunked HTTP body cap failed or exposed a partial response");
  response =
      completed(submit(client, {.url = fixture.url(), .maxResponseBytes = 0}));
  require(response.ok && response.body == "abcdef",
          "unlimited HTTP body setting failed");
  Fixture fixed(wire(std::string(65536, 'x')));
  response =
      completed(submit(client, {.url = fixed.url(), .maxResponseBytes = 1024}));
  require(response.errorCode == "response_too_large",
          "Content-Length body bypassed the cap");
  constexpr std::size_t LargeBodyBytes = 16 * 1024 * 1024 + 1;
  Fixture large(wire(std::string(LargeBodyBytes, 'x')));
  response =
      completed(submit(client, {.url = large.url(), .maxResponseBytes = 0}));
  require(response.ok && response.body.size() == LargeBodyBytes,
          "unlimited HTTP body retained the default cap");
  response = completed(
      submit(client, {.url = large.url(), .maxResponseBytes = LargeBodyBytes}));
  require(response.ok && response.body.size() == LargeBodyBytes,
          "custom HTTP body limit retained the default cap");
}

void contentDecoding() {
  // Fixed gzip (mtime=0) and zlib-wrapped deflate representations of the JSON
  // below. Tests do not need to link a separate compression library.
  constexpr char Gzip[] =
      "\x1f\x8b\x08\x00\x00\x00\x00\x00\x02\xff\xed\xc1\x31\x0d\x00\x20"
      "\x10\x04\x30\x2f\x27\x03\x37\x3f\x7c\x98\x98\x18\x09\xde\xf1\x41"
      "\xda\x9e\xac\xde\xbb\x66\x67\xa4\x00\x00\x00\x80\xef\xe5\x3e\xbb"
      "\x9d\x60\x0f\x0e\x10\x00\x00";
  constexpr char Deflate[] =
      "\x78\xda\xed\xc1\x31\x0d\x00\x20\x10\x04\x30\x2f\x27\x03\x37\x3f"
      "\x7c\x98\x98\x18\x09\xde\xf1\x41\xda\x9e\xac\xde\xbb\x66\x67\xa4"
      "\x00\x00\x00\x80\xef\xe5\x3e\x39\xbc\x14\xfa";
  const std::string decoded =
      "{\"message\":\"" + std::string(4096, 'a') + "\"}";
  const std::pair<std::string_view, std::string_view> encodings[] = {
      {"gzip", {Gzip, sizeof(Gzip) - 1}},
      {"deflate", {Deflate, sizeof(Deflate) - 1}}};

  HttpClient client;
  Fixture probe(wire("identity"));
  require(completed(submit(client, {.url = probe.url()})).ok,
          "HTTP encoding negotiation probe failed");
  const auto sent = probe.received();
  constexpr std::string_view Header = "\r\nAccept-Encoding: ";
  const auto start = sent.find(Header);
  require(start != std::string::npos,
          "HTTP did not advertise supported content encodings");
  const auto begin = start + Header.size();
  const auto advertised = sent.substr(begin, sent.find("\r\n", begin) - begin);

  for (const auto &[encoding, encoded] : encodings) {
    const auto headers =
        "Content-Type: application/json\r\nContent-Encoding: " +
        std::string(encoding) + "\r\n";
    Fixture fixture(wire(std::string(encoded), "200 OK", headers));
    auto response = completed(submit(
        client, {.url = fixture.url(), .maxResponseBytes = decoded.size()}));
    if (advertised.find(encoding) == std::string::npos) {
      // A valid curl build may omit zlib. It must reject an unsupported server
      // encoding rather than return compressed bytes as a successful body.
      require(!response.ok && response.errorCode == "transport",
              "unsupported HTTP content encoding was accepted");
      std::cout
          << "NOTE HTTP " << encoding
          << " decoder unavailable; unsupported-encoding rejection checked\n";
      continue;
    }
    require(response.ok && response.body == decoded,
            "HTTP compressed JSON or exact decoded limit failed");
    require(std::ranges::find(response.headers,
                              std::pair{std::string("Content-Encoding"),
                                        std::string(encoding)}) !=
                response.headers.end(),
            "HTTP content decoding modified the original encoding header");
    require(std::ranges::find(response.headers,
                              std::pair{std::string("Content-Length"),
                                        std::to_string(encoded.size())}) !=
                response.headers.end(),
            "HTTP content decoding modified the original encoded length");

    response =
        completed(submit(client, {.url = fixture.url(),
                                  .maxResponseBytes = decoded.size() - 1}));
    require(encoded.size() < decoded.size() - 1 && !response.ok &&
                response.errorCode == "response_too_large" &&
                response.body.empty(),
            "HTTP response limit counted compressed bytes instead of decoded "
            "bytes");
    response = completed(
        submit(client, {.url = fixture.url(), .maxResponseBytes = 0}));
    require(response.ok && response.body == decoded,
            "unlimited HTTP compressed response failed");

    response = completed(submit(
        client, {.url = fixture.url(),
                 .headers = {{"Accept-Encoding", std::string(encoding)}}}));
    require(response.ok && response.body == decoded,
            "explicit Accept-Encoding disabled HTTP content decoding");
    const auto overridden = fixture.received();
    const auto expectedHeader =
        std::string(Header) + std::string(encoding) + "\r\n";
    const auto position = overridden.find(expectedHeader);
    require(position != std::string::npos &&
                overridden.find(Header, position + Header.size()) ==
                    std::string::npos,
            "explicit Accept-Encoding did not replace automatic negotiation");

    Fixture damaged(wire("not compressed: private-body", "200 OK", headers));
    response = completed(submit(client, {.url = damaged.url()}));
    require(!response.ok && response.errorCode == "transport" &&
                response.body.empty() &&
                response.error.find("private-body") == std::string::npos,
            "malformed HTTP compression did not produce a sanitized transport "
            "error");
  }

  Fixture unsupported(
      wire("private-body", "200 OK", "Content-Encoding: demi-unsupported\r\n"));
  const auto response = completed(
      submit(client, {.url = unsupported.url(),
                      .headers = {{"Accept-Encoding", "demi-unsupported"}}}));
  require(
      !response.ok && response.errorCode == "transport" &&
          response.body.empty(),
      "explicit Accept-Encoding bypassed the transport's supported decoders");
}

void headerLimits() {
  const std::string interim =
      "HTTP/1.1 103 Early Hints\r\nX-Interim: discarded\r\n\r\n";
  const auto raw = interim + wire("ok", "200 OK", "X-Final: retained\r\n");
  const auto headerBytes = raw.size() - 2;
  Fixture fixture(raw);
  HttpClient client;
  auto response = completed(
      submit(client, {.url = fixture.url(), .maxHeaderBytes = headerBytes}));
  require(response.ok && response.body == "ok",
          "exact cumulative HTTP header limit failed");
  response = completed(submit(
      client, {.url = fixture.url(), .maxHeaderBytes = headerBytes - 1}));
  require(!response.ok && response.errorCode == "response_too_large" &&
              response.headers.empty(),
          "interim HTTP headers bypassed the cumulative limit");

  const std::string chunked =
      "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n"
      "2\r\nok\r\n0\r\nX-Trailer: limited\r\n\r\n";
  Fixture trailer(chunked);
  response = completed(
      submit(client, {.url = trailer.url(),
                      .maxHeaderBytes = chunked.find("\r\n\r\n") + 4}));
  require(response.errorCode == "response_too_large",
          "HTTP trailers bypassed the header limit");

  std::string headers;
  constexpr std::size_t HeaderCount = 80;
  for (std::size_t i = 0; i < HeaderCount; ++i) {
    headers += "X-Repeated: " + std::string(1024, 'x') + "\r\n";
  }
  const auto largeRaw = wire("ok", "200 OK", headers);
  Fixture large(largeRaw);
  response = completed(submit(client, {.url = large.url()}));
  require(response.errorCode == "response_too_large",
          "default HTTP header limit did not bound many small headers");
  response =
      completed(submit(client, {.url = large.url(), .maxHeaderBytes = 0}));
  require(response.ok && response.headers.size() == HeaderCount + 2,
          "unlimited HTTP headers retained the default cap");
  response = completed(submit(
      client, {.url = large.url(), .maxHeaderBytes = largeRaw.size() - 2}));
  require(response.ok && response.headers.size() == HeaderCount + 2,
          "custom HTTP headers retained the default cap");
}

void timeoutAndConcurrency() {
  Fixture slow(wire("late"), false, 1000ms);
  Fixture fast(wire("fast"));
  HttpClient client;
  const auto start = Clock::now();
  auto delayed = submit(client, {.url = slow.url(), .timeoutMs = 0});
  require(Clock::now() - start < 500ms,
          "HTTP submission blocked on network I/O");
  require(!delayed->done() && !delayed->response(),
          "pending HTTP response did not return nullopt");
  slow.awaitRequest();
  const auto quick = completed(submit(client, {.url = fast.url()}));
  require(quick.ok && quick.body == "fast" && !delayed->done(),
          "slow transfer blocked another HTTP request");
  delayed->cancel();
  Fixture timed(wire("late"), false, 1000ms);
  const auto timeout =
      completed(submit(client, {.url = timed.url(), .timeoutMs = 100}));
  require(timeout.errorCode == "timeout" && !timeout.ok,
          "delayed HTTP response did not time out");
  Fixture unlimited(wire("eventual"), false, 150ms);
  require(
      completed(submit(client, {.url = unlimited.url(), .timeoutMs = 0})).ok,
      "zero overall timeout was not unlimited");
}

void cancellationAndShutdown() {
  Fixture fixture(wire("late"), false, 2000ms);
  std::shared_ptr<HttpOperation> survivor;
  {
    HttpClient client;
    auto cancelled = submit(client, {.url = fixture.url(), .timeoutMs = 0});
    fixture.awaitRequest();
    auto start = Clock::now();
    cancelled->cancel();
    cancelled->cancel();
    require(cancelled->done() &&
                completed(cancelled).errorCode == "cancelled" &&
                Clock::now() - start < 500ms,
            "HTTP cancel was not prompt and idempotent");
    std::vector<std::shared_ptr<HttpOperation>> operations;
    // Exercise queued and active cancellation beyond small arbitrary caps.
    for (int i = 0; i < 192; ++i) {
      operations.push_back(
          submit(client, {.url = fixture.url(), .timeoutMs = 0}));
    }
    survivor = operations.back();
    start = Clock::now();
    client.shutdown();
    require(Clock::now() - start < 1000ms,
            "HTTP shutdown waited for server timeout");
    client.shutdown();
    for (const auto &operation : operations) {
      require(operation->done() &&
                  operation->response()->errorCode == "cancelled",
              "HTTP shutdown left a pending handle or wrong terminal state");
    }
  }
  survivor->cancel();
  require(survivor->response()->errorCode == "cancelled",
          "HTTP handle did not survive client destruction");
  {
    HttpClient client;
    survivor = submit(client, {.url = fixture.url(), .timeoutMs = 0});
  }
  require(survivor->done() && survivor->response()->errorCode == "cancelled",
          "HTTP destructor did not cancel pending work");
  HttpClient unused;
  unused.shutdown();
  unused.shutdown();
}

void tlsVerification() {
  Fixture fixture(wire("verified"), true);
  HttpClient client;
  const auto ca = readFile(DEMI_DTLS_TEST_CA);
  auto request = HttpRequest{.url = fixture.url(true), .caCertificatePem = ca};
  auto response = completed(submit(client, request));
  require(response.ok && response.body == "verified",
          "HTTPS trusted hostname did not verify");
  request.url =
      fixture.url(false); // certificate CN is localhost, not 127.0.0.1.
  response = completed(submit(client, request));
  require(!response.ok && response.errorCode == "tls",
          "HTTPS accepted a mismatched hostname");
  request.url = fixture.url(true);
  request.caCertificatePem.clear();
  response = completed(submit(client, request));
  require(!response.ok && response.errorCode == "tls",
          "HTTPS accepted an untrusted test CA");
  request.caCertificatePem = "not a certificate: secret";
  response = completed(submit(client, request));
  require(response.errorCode == "tls" &&
              response.error.find("secret") == std::string::npos,
          "HTTPS malformed trust error leaked input or used default trust");
}

void tlsHandshakeTimeout() {
  // This plain fixture accepts TCP but never speaks TLS. It exercises the
  // connection/handshake deadline without unreliable unroutable-address probes.
  Fixture silent("");
  auto url = silent.url();
  url.replace(0, 4, "https");
  HttpClient client;
  const auto ca = readFile(DEMI_DTLS_TEST_CA);
  const auto response = completed(submit(client, {.url = url,
                                                  .timeoutMs = 0,
                                                  .connectTimeoutMs = 100,
                                                  .caCertificatePem = ca}));
  require(!response.ok && response.errorCode == "timeout",
          "HTTPS handshake ignored the connection timeout");
  const auto pending = submit(client, {.url = url,
                                       .timeoutMs = 0,
                                       .connectTimeoutMs = 0,
                                       .caCertificatePem = ca});
  client.shutdown();
  require(pending->done() && pending->response()->errorCode == "cancelled",
          "HTTPS handshake was not cancelled during shutdown");
}

void transportDiagnostics() {
  Fixture broken("this is not HTTP\r\n");
  HttpClient client;
  auto request = HttpRequest{.url = broken.url() + "?secret=private",
                             .method = "POST",
                             .headers = {{"Authorization", "Bearer private"}},
                             .body = "private-body"};
  const auto response = completed(submit(client, request));
  require(!response.ok && response.errorCode == "transport" &&
              !response.error.empty(),
          "malformed response did not produce a transport error");
  for (const auto *secret : {"127.0.0.1", "secret", "private", "Bearer"}) {
    require(response.error.find(secret) == std::string::npos,
            "transport diagnostic exposed request data");
  }
}

void luaResponseContract() {
  Fixture fixture(wire(
      R"({"items":[1,null,3],"empty":[],"large":4294967296})", "200 OK",
      "Content-Type: application/json\r\nX-Probe: one\r\nX-Probe: two\r\n"));
  demi::runtime::World world;
  demi::runtime::InputState input;
  demi::runtime::LuaScriptHost host;
  std::string error;
  require(host.initialize(world, input, nullptr, error),
          "Lua HTTP host failed to initialize");
  const auto start = host.executeConsole(
      "http_test_request = assert(require('demi.network.http').post(" +
      nlohmann::json(fixture.url()).dump() + R"lua(, {
        json = { score = 42, optional = require('demi.data').null },
        headers = { Authorization = 'Bearer fixture-only' }
      }))
      assert(type(http_test_request) == 'userdata')
    )lua");
  require(start.succeeded, start.error.c_str());
  waitFor(
      [&] {
        const auto probe =
            host.executeConsole("return http_test_request:done()");
        require(probe.succeeded, probe.error.c_str());
        return !probe.values.empty() && probe.values.front() == "true";
      },
      "Lua HTTP request did not complete");
  const auto check = host.executeConsole(R"lua(
    local Data = require('demi.data')
    local response = http_test_request:response()
    assert(response.ok and response.status == 200 and response.error == '')
    assert(response.json_error == '' and response.json.large == 4294967296)
    assert(response.json.items[1] == 1 and response.json.items[3] == 3)
    assert(Data.is_null(response.json.items[2]))
    assert(Data.kind(response.json.items) == 'array')
    assert(Data.kind(response.json.empty) == 'array' and #response.json.empty == 0)
    assert(response.headers['x-probe'][1] == 'one')
    assert(response.headers['x-probe'][2] == 'two')
    assert(http_test_request:response().body == response.body)
  )lua");
  require(check.succeeded, check.error.c_str());
  const auto sent = fixture.received();
  require(sent.find("Content-Type: application/json") != std::string::npos,
          "Lua JSON request omitted its content type");
  require(sent.find("Authorization: Bearer fixture-only") != std::string::npos,
          "Lua request omitted its authorization header");
  const auto bodyStart = sent.find("\r\n\r\n");
  require(bodyStart != std::string::npos,
          "Lua request had no HTTP body delimiter");
  const auto body = nlohmann::json::parse(sent.substr(bodyStart + 4));
  require(body["score"] == 42 && body["optional"].is_null(),
          "Lua JSON request lost scalar or null values");
}

} // namespace

int main() {
  const std::pair<const char *, void (*)()> tests[] = {
      {"Lua response contract", luaResponseContract},
      {"validation", validation},
      {"binary and headers", binaryAndHeaders},
      {"statuses and redirects", statusesAndRedirects},
      {"chunked and limits", chunkedAndLimits},
      {"content decoding", contentDecoding},
      {"header limits", headerLimits},
      {"timeout and concurrency", timeoutAndConcurrency},
      {"cancellation and shutdown", cancellationAndShutdown},
      {"TLS verification", tlsVerification},
      {"TLS handshake timeout", tlsHandshakeTimeout},
      {"transport diagnostics", transportDiagnostics}};
  int failures = 0;
  std::size_t passed = 0;
  try {
    const ProxyEnvironmentFixture environment;
    for (const auto &[name, test] : tests) {
      try {
        test();
        ++passed;
        std::cout << "PASS HTTP " << name << '\n';
      } catch (const std::exception &error) {
        ++failures;
        std::cerr << "FAIL HTTP " << name << ": " << error.what() << '\n';
      }
    }
  } catch (const std::exception &error) {
    ++failures;
    std::cerr << "FAIL HTTP fixture setup: " << error.what() << '\n';
  }
  std::cout << "HTTP tests: " << passed << " passed, " << failures
            << " failed\n";
  return failures ? 1 : 0;
}
