#include "demi/runtime/network/HttpClient.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cctype>
#include <cstring>
#include <sstream>
#include <string_view>

#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#if DEMI_HAS_MBEDTLS
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/ssl.h>
#endif

namespace demi::runtime {

namespace {

struct SocketHandle {
  int fd = -1;

  ~SocketHandle() {
    if (fd >= 0) {
      close(fd);
    }
  }
};

[[nodiscard]] bool setNonBlocking(const int fd, std::string& error) {
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    error = "failed to configure socket";
    return false;
  }
  return true;
}

[[nodiscard]] int connectSocket(const ParsedUrl& url, const int timeoutMs, std::string& error) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo* results = nullptr;
  const std::string port = std::to_string(url.port);
  const int gai = getaddrinfo(url.host.c_str(), port.c_str(), &hints, &results);
  if (gai != 0) {
    error = gai_strerror(gai);
    return -1;
  }

  for (addrinfo* item = results; item != nullptr; item = item->ai_next) {
    SocketHandle socket{::socket(item->ai_family, item->ai_socktype, item->ai_protocol)};
    if (socket.fd < 0 || !setNonBlocking(socket.fd, error)) {
      continue;
    }

    const int result = ::connect(socket.fd, item->ai_addr, item->ai_addrlen);
    if (result == 0 || errno == EINPROGRESS) {
      pollfd pollItem{.fd = socket.fd, .events = POLLOUT, .revents = 0};
      if (poll(&pollItem, 1, timeoutMs) > 0) {
        int socketError = 0;
        socklen_t length = sizeof(socketError);
        if (getsockopt(socket.fd, SOL_SOCKET, SO_ERROR, &socketError, &length) == 0 && socketError == 0) {
          const int fd = socket.fd;
          socket.fd = -1;
          freeaddrinfo(results);
          return fd;
        }
      }
    }
  }

  freeaddrinfo(results);
  if (error.empty()) {
    error = "connection failed";
  }
  return -1;
}

[[nodiscard]] bool sendAll(const int fd, const std::string& request, const int timeoutMs, std::string& error) {
  std::size_t sent = 0;
  while (sent < request.size()) {
    pollfd pollItem{.fd = fd, .events = POLLOUT, .revents = 0};
    if (poll(&pollItem, 1, timeoutMs) <= 0) {
      error = "send timed out";
      return false;
    }
    const ssize_t count = send(fd, request.data() + sent, request.size() - sent, 0);
    if (count < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        continue;
      }
      error = "send failed";
      return false;
    }
    sent += static_cast<std::size_t>(count);
  }
  return true;
}

[[nodiscard]] std::string readAll(const int fd, const int timeoutMs, std::string& error) {
  std::string response;
  std::array<char, 4096> buffer{};
  while (true) {
    pollfd pollItem{.fd = fd, .events = POLLIN, .revents = 0};
    const int ready = poll(&pollItem, 1, timeoutMs);
    if (ready == 0) {
      error = "receive timed out";
      return {};
    }
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      error = "receive failed";
      return {};
    }
    const ssize_t count = recv(fd, buffer.data(), buffer.size(), 0);
    if (count == 0) {
      break;
    }
    if (count < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        continue;
      }
      error = "receive failed";
      return {};
    }
    response.append(buffer.data(), static_cast<std::size_t>(count));
  }
  return response;
}

[[nodiscard]] HttpResponse parseResponse(const std::string& raw) {
  HttpResponse response;
  const std::size_t headerEnd = raw.find("\r\n\r\n");
  if (headerEnd == std::string::npos) {
    response.error = "invalid HTTP response";
    return response;
  }

  const std::string_view statusLine(raw.data(), raw.find("\r\n"));
  const std::size_t firstSpace = statusLine.find(' ');
  if (firstSpace != std::string_view::npos) {
    const std::size_t secondSpace = statusLine.find(' ', firstSpace + 1);
    const std::string_view statusText = statusLine.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    (void)std::from_chars(statusText.data(), statusText.data() + statusText.size(), response.status);
  }
  const std::string headers = raw.substr(0, headerEnd);
  std::string lowerHeaders = headers;
  std::ranges::transform(lowerHeaders, lowerHeaders.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  response.body = raw.substr(headerEnd + 4);
  if (lowerHeaders.find("transfer-encoding: chunked") != std::string::npos) {
    std::string decoded;
    std::size_t offset = 0;
    while (offset < response.body.size()) {
      const std::size_t lineEnd = response.body.find("\r\n", offset);
      if (lineEnd == std::string::npos) {
        response.error = "invalid chunked HTTP response";
        response.body.clear();
        return response;
      }
      const std::string_view sizeText(response.body.data() + offset, lineEnd - offset);
      std::size_t chunkSize = 0;
      const auto [_, ec] = std::from_chars(sizeText.data(), sizeText.data() + sizeText.size(), chunkSize, 16);
      if (ec != std::errc{}) {
        response.error = "invalid HTTP chunk size";
        response.body.clear();
        return response;
      }
      offset = lineEnd + 2;
      if (chunkSize == 0) {
        break;
      }
      if (offset + chunkSize > response.body.size()) {
        response.error = "truncated chunked HTTP response";
        response.body.clear();
        return response;
      }
      decoded.append(response.body.data() + offset, chunkSize);
      offset += chunkSize + 2;
    }
    response.body = std::move(decoded);
  }
  response.ok = response.status >= 200 && response.status < 300;
  return response;
}

[[nodiscard]] std::string tlsError(const int code) {
#if DEMI_HAS_MBEDTLS
  std::array<char, 160> buffer{};
  mbedtls_strerror(code, buffer.data(), buffer.size());
  return buffer.data();
#else
  (void)code;
  return "TLS backend unavailable";
#endif
}

[[nodiscard]] HttpResponse tlsRequest(const std::string& method, const ParsedUrl& url, const std::string& body) {
  HttpResponse response;
#if !DEMI_HAS_MBEDTLS
  response.error = "TLS backend unavailable";
  return response;
#else
  mbedtls_net_context server;
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;
  mbedtls_ctr_drbg_context ctrDrbg;
  mbedtls_entropy_context entropy;

  mbedtls_net_init(&server);
  mbedtls_ssl_init(&ssl);
  mbedtls_ssl_config_init(&conf);
  mbedtls_ctr_drbg_init(&ctrDrbg);
  mbedtls_entropy_init(&entropy);

  auto cleanup = [&] {
    mbedtls_ssl_close_notify(&ssl);
    mbedtls_net_free(&server);
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&ctrDrbg);
    mbedtls_entropy_free(&entropy);
  };

  constexpr const char* Personalization = "demi-http-client";
  int result = mbedtls_ctr_drbg_seed(&ctrDrbg, mbedtls_entropy_func, &entropy,
                                     reinterpret_cast<const unsigned char*>(Personalization),
                                     std::strlen(Personalization));
  if (result != 0) {
    response.error = tlsError(result);
    cleanup();
    return response;
  }

  result = mbedtls_net_connect(&server, url.host.c_str(), std::to_string(url.port).c_str(), MBEDTLS_NET_PROTO_TCP);
  if (result != 0) {
    response.error = tlsError(result);
    cleanup();
    return response;
  }

  result = mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
  if (result != 0) {
    response.error = tlsError(result);
    cleanup();
    return response;
  }
  mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctrDrbg);

  result = mbedtls_ssl_setup(&ssl, &conf);
  if (result != 0) {
    response.error = tlsError(result);
    cleanup();
    return response;
  }
  result = mbedtls_ssl_set_hostname(&ssl, url.host.c_str());
  if (result != 0) {
    response.error = tlsError(result);
    cleanup();
    return response;
  }
  mbedtls_ssl_set_bio(&ssl, &server, mbedtls_net_send, mbedtls_net_recv, nullptr);

  while ((result = mbedtls_ssl_handshake(&ssl)) != 0) {
    if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
      response.error = tlsError(result);
      cleanup();
      return response;
    }
  }

  std::ostringstream request;
  request << method << ' ' << url.path << " HTTP/1.1\r\n"
          << "Host: " << url.host << "\r\n"
          << "Connection: close\r\n"
          << "User-Agent: DemiEngine/0.1\r\n";
  if (method == "POST") {
    request << "Content-Type: application/x-www-form-urlencoded\r\n"
            << "Content-Length: " << body.size() << "\r\n";
  }
  request << "\r\n" << body;

  const std::string text = request.str();
  std::size_t written = 0;
  while (written < text.size()) {
    result = mbedtls_ssl_write(&ssl, reinterpret_cast<const unsigned char*>(text.data() + written), text.size() - written);
    if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) {
      continue;
    }
    if (result <= 0) {
      response.error = tlsError(result);
      cleanup();
      return response;
    }
    written += static_cast<std::size_t>(result);
  }

  std::string raw;
  std::array<unsigned char, 4096> buffer{};
  while (true) {
    result = mbedtls_ssl_read(&ssl, buffer.data(), buffer.size());
    if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) {
      continue;
    }
    if (result == 0 || result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
      break;
    }
    if (result < 0) {
      response.error = tlsError(result);
      cleanup();
      return response;
    }
    raw.append(reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(result));
  }

  cleanup();
  return parseResponse(raw);
#endif
}

[[nodiscard]] HttpResponse request(const std::string& method, const std::string& urlText, const std::string& body, const int timeoutMs) {
  ParsedUrl url;
  HttpResponse response;
  if (!parseHttpUrl(urlText, url, response.error)) {
    return response;
  }
  if (url.scheme == "https") {
    (void)timeoutMs;
    return tlsRequest(method, url, body);
  }

  SocketHandle socket{connectSocket(url, timeoutMs, response.error)};
  if (socket.fd < 0) {
    return response;
  }

  std::ostringstream request;
  request << method << ' ' << url.path << " HTTP/1.1\r\n"
          << "Host: " << url.host << "\r\n"
          << "Connection: close\r\n"
          << "User-Agent: DemiEngine/0.1\r\n";
  if (method == "POST") {
    request << "Content-Type: application/x-www-form-urlencoded\r\n"
            << "Content-Length: " << body.size() << "\r\n";
  }
  request << "\r\n" << body;

  if (!sendAll(socket.fd, request.str(), timeoutMs, response.error)) {
    return response;
  }
  const std::string raw = readAll(socket.fd, timeoutMs, response.error);
  if (!response.error.empty()) {
    return response;
  }
  return parseResponse(raw);
}

} // namespace

std::string urlEncode(const std::string& value) {
  std::ostringstream output;
  output.fill('0');
  output << std::hex << std::uppercase;
  for (const unsigned char c : value) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
      output << static_cast<char>(c);
    } else if (c == ' ') {
      output << '+';
    } else {
      output << '%' << static_cast<int>(c >> 4) << static_cast<int>(c & 15);
    }
  }
  return output.str();
}

std::string formEncode(const std::map<std::string, std::string>& fields) {
  std::string body;
  for (const auto& [key, value] : fields) {
    if (!body.empty()) {
      body += '&';
    }
    body += urlEncode(key);
    body += '=';
    body += urlEncode(value);
  }
  return body;
}

bool parseHttpUrl(const std::string& url, ParsedUrl& parsed, std::string& error) {
  constexpr std::string_view httpPrefix = "http://";
  constexpr std::string_view httpsPrefix = "https://";
  parsed = ParsedUrl{};
  std::size_t prefixSize = httpPrefix.size();
  if (url.starts_with(httpsPrefix)) {
    prefixSize = httpsPrefix.size();
    parsed.scheme = "https";
    parsed.port = 443;
  } else if (url.starts_with(httpPrefix)) {
    parsed.scheme = "http";
    parsed.port = 80;
  } else {
    error = "URL must start with http:// or https://";
    return false;
  }

  std::string remainder = url.substr(prefixSize);
  const std::size_t pathStart = remainder.find('/');
  std::string hostPort = pathStart == std::string::npos ? remainder : remainder.substr(0, pathStart);
  parsed.path = pathStart == std::string::npos ? "/" : remainder.substr(pathStart);
  if (hostPort.empty()) {
    error = "URL host is empty";
    return false;
  }

  const std::size_t colon = hostPort.rfind(':');
  parsed.host = colon == std::string::npos ? hostPort : hostPort.substr(0, colon);
  if (colon != std::string::npos) {
    int port = 0;
    const std::string_view portText(hostPort.data() + colon + 1, hostPort.size() - colon - 1);
    const auto [_, ec] = std::from_chars(portText.data(), portText.data() + portText.size(), port);
    if (ec != std::errc{} || port <= 0 || port > 65535) {
      error = "URL port is invalid";
      return false;
    }
    parsed.port = static_cast<std::uint16_t>(port);
  }
  if (parsed.host.empty()) {
    error = "URL host is empty";
    return false;
  }
  return true;
}

HttpResponse httpGet(const std::string& url, const int timeoutMs) {
  return request("GET", url, {}, timeoutMs);
}

HttpResponse httpPostForm(const std::string& url, const std::map<std::string, std::string>& fields, const int timeoutMs) {
  return request("POST", url, formEncode(fields), timeoutMs);
}

} // namespace demi::runtime
