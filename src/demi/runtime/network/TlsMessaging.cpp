#include "demi/runtime/network/TlsMessaging.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <sys/socket.h>
#include <unordered_map>
#include <unistd.h>
#include <utility>

namespace demi::runtime {
namespace {

constexpr std::size_t MaxFrame = 64 * 1024;

// The matchmaking UI calls connect from the frame loop. Keep TCP setup out of it.
int beginNonBlockingConnect(const std::string& host, const std::uint16_t port, int& socketFd, std::string& error) {
  addrinfo hints{};
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo* results = nullptr;
  const int lookup = getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &results);
  if (lookup != 0) { error = gai_strerror(lookup); return -1; }
  for (addrinfo* item = results; item != nullptr; item = item->ai_next) {
    const int fd = ::socket(item->ai_family, item->ai_socktype, item->ai_protocol);
    if (fd < 0) continue;
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) { close(fd); continue; }
    const int result = ::connect(fd, item->ai_addr, item->ai_addrlen);
    if (result == 0) { socketFd = fd; freeaddrinfo(results); return 0; }
    if (errno == EINPROGRESS) { socketFd = fd; freeaddrinfo(results); return 1; }
    close(fd);
  }
  freeaddrinfo(results);
  error = "TCP connection failed";
  return -1;
}

bool finishNonBlockingConnect(const int fd, std::string& error) {
  pollfd item{.fd = fd, .events = POLLOUT, .revents = 0};
  if (poll(&item, 1, 0) <= 0) return false;
  int socketError = 0;
  socklen_t length = sizeof(socketError);
  if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &length) != 0 || socketError != 0) {
    error = socketError != 0 ? std::strerror(socketError) : "TCP connection failed";
    return true;
  }
  return true;
}

std::string tlsError(const int code) {
  std::array<char, 192> text{};
  mbedtls_strerror(code, text.data(), text.size());
  return text.data();
}

std::string frame(const std::string& message) {
  const std::uint32_t size = static_cast<std::uint32_t>(message.size());
  std::string result(4, '\0');
  result[0] = static_cast<char>((size >> 24U) & 0xffU);
  result[1] = static_cast<char>((size >> 16U) & 0xffU);
  result[2] = static_cast<char>((size >> 8U) & 0xffU);
  result[3] = static_cast<char>(size & 0xffU);
  result += message;
  return result;
}

bool extractFrames(std::string& input, std::vector<std::string>& messages) {
  while (input.size() >= 4) {
    const auto* p = reinterpret_cast<const unsigned char*>(input.data());
    const std::size_t size = (static_cast<std::size_t>(p[0]) << 24U) |
                             (static_cast<std::size_t>(p[1]) << 16U) |
                             (static_cast<std::size_t>(p[2]) << 8U) | p[3];
    if (size > MaxFrame) return false;
    if (input.size() < size + 4) return true;
    messages.emplace_back(input.substr(4, size));
    input.erase(0, size + 4);
  }
  return true;
}

struct Connection {
  mbedtls_net_context net{};
  mbedtls_ssl_context ssl{};
  std::string input;
  std::deque<std::string> output;
  std::size_t outputOffset = 0;
  bool handshakeComplete = false;

  Connection() { mbedtls_net_init(&net); mbedtls_ssl_init(&ssl); }
  ~Connection() { mbedtls_ssl_free(&ssl); mbedtls_net_free(&net); }
};

int pump(Connection& connection, std::vector<std::string>& messages) {
  if (!connection.handshakeComplete) {
    const int result = mbedtls_ssl_handshake(&connection.ssl);
    if (result == 0) connection.handshakeComplete = true;
    else if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) return result;
  }
  if (!connection.handshakeComplete) return 0;

  while (!connection.output.empty()) {
    const std::string& value = connection.output.front();
    const int result = mbedtls_ssl_write(&connection.ssl,
      reinterpret_cast<const unsigned char*>(value.data() + connection.outputOffset),
      value.size() - connection.outputOffset);
    if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) break;
    if (result <= 0) return result;
    connection.outputOffset += static_cast<std::size_t>(result);
    if (connection.outputOffset == value.size()) {
      connection.output.pop_front();
      connection.outputOffset = 0;
    }
  }

  std::array<unsigned char, 4096> buffer{};
  while (true) {
    const int result = mbedtls_ssl_read(&connection.ssl, buffer.data(), buffer.size());
    if (result > 0) connection.input.append(reinterpret_cast<const char*>(buffer.data()), result);
    else if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) break;
    else return result == 0 ? MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY : result;
  }
  return extractFrames(connection.input, messages) ? 0 : MBEDTLS_ERR_SSL_BAD_INPUT_DATA;
}

struct TlsConfig {
  mbedtls_entropy_context entropy{};
  mbedtls_ctr_drbg_context random{};
  mbedtls_ssl_config ssl{};
  mbedtls_x509_crt certificate{};
  mbedtls_pk_context key{};
  TlsConfig() {
    mbedtls_entropy_init(&entropy); mbedtls_ctr_drbg_init(&random);
    mbedtls_ssl_config_init(&ssl); mbedtls_x509_crt_init(&certificate); mbedtls_pk_init(&key);
  }
  ~TlsConfig() {
    mbedtls_pk_free(&key); mbedtls_x509_crt_free(&certificate);
    mbedtls_ssl_config_free(&ssl); mbedtls_ctr_drbg_free(&random); mbedtls_entropy_free(&entropy);
  }
  void reset() {
    mbedtls_pk_free(&key); mbedtls_x509_crt_free(&certificate);
    mbedtls_ssl_config_free(&ssl); mbedtls_ctr_drbg_free(&random);
    mbedtls_pk_init(&key); mbedtls_x509_crt_init(&certificate);
    mbedtls_ssl_config_init(&ssl); mbedtls_ctr_drbg_init(&random);
  }
  bool seed(std::string& error) {
    static constexpr unsigned char Personalization[] = "Demi TLS messaging";
    const int result = mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy,
      Personalization, sizeof(Personalization) - 1);
    if (result != 0) error = tlsError(result);
    return result == 0;
  }
};

bool configureTlsClient(TlsConfig& config, Connection& connection, const std::string& serverName, std::string& error) {
  int result = mbedtls_ssl_setup(&connection.ssl, &config.ssl);
  if (result == 0) result = mbedtls_ssl_set_hostname(&connection.ssl, serverName.c_str());
  if (result != 0) { error = tlsError(result); return false; }
  mbedtls_ssl_set_bio(&connection.ssl, &connection.net, mbedtls_net_send, mbedtls_net_recv, nullptr);
  return true;
}

} // namespace

struct TlsServer::Impl {
  TlsConfig config;
  mbedtls_net_context listener{};
  std::unordered_map<std::uint32_t, std::unique_ptr<Connection>> clients;
  std::vector<TlsEvent> events;
  std::string error;
  std::uint32_t nextId = 1;
  std::uint32_t maxClients = 64;
  Impl() { mbedtls_net_init(&listener); }
  ~Impl() { mbedtls_net_free(&listener); }
};

TlsServer::TlsServer() : impl_(std::make_unique<Impl>()) {}
TlsServer::~TlsServer() = default;

bool TlsServer::listen(const std::uint16_t port, const std::filesystem::path& certificate,
                       const std::filesystem::path& privateKey, const std::uint32_t maxClients) {
  stop();
  impl_->config.reset();
  impl_->maxClients = std::max(maxClients, 1U);
  if (!impl_->config.seed(impl_->error)) return false;
  int result = mbedtls_x509_crt_parse_file(&impl_->config.certificate, certificate.string().c_str());
  if (result == 0) result = mbedtls_pk_parse_keyfile(&impl_->config.key, privateKey.string().c_str(), nullptr,
                                                     mbedtls_ctr_drbg_random, &impl_->config.random);
  if (result == 0) result = mbedtls_ssl_config_defaults(&impl_->config.ssl, MBEDTLS_SSL_IS_SERVER,
                                                        MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
  if (result == 0) result = mbedtls_ssl_conf_own_cert(&impl_->config.ssl, &impl_->config.certificate, &impl_->config.key);
  if (result != 0) { impl_->error = tlsError(result); return false; }
  mbedtls_ssl_conf_rng(&impl_->config.ssl, mbedtls_ctr_drbg_random, &impl_->config.random);
  mbedtls_ssl_conf_min_version(&impl_->config.ssl, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
  mbedtls_ssl_conf_max_version(&impl_->config.ssl, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
  result = mbedtls_net_bind(&impl_->listener, nullptr, std::to_string(port).c_str(), MBEDTLS_NET_PROTO_TCP);
  if (result == 0) result = mbedtls_net_set_nonblock(&impl_->listener);
  if (result != 0) { impl_->error = tlsError(result); return false; }
  return true;
}

void TlsServer::update() {
  while (impl_->clients.size() < impl_->maxClients && impl_->listener.fd >= 0) {
    auto client = std::make_unique<Connection>();
    const int accepted = mbedtls_net_accept(&impl_->listener, &client->net, nullptr, 0, nullptr);
    if (accepted == MBEDTLS_ERR_SSL_WANT_READ) break;
    if (accepted != 0) { impl_->error = tlsError(accepted); break; }
    (void)mbedtls_net_set_nonblock(&client->net);
    const int setup = mbedtls_ssl_setup(&client->ssl, &impl_->config.ssl);
    if (setup != 0) { impl_->error = tlsError(setup); continue; }
    mbedtls_ssl_set_bio(&client->ssl, &client->net, mbedtls_net_send, mbedtls_net_recv, nullptr);
    impl_->clients.emplace(impl_->nextId++, std::move(client));
  }

  std::vector<std::uint32_t> remove;
  for (auto& [id, client] : impl_->clients) {
    const bool wasConnected = client->handshakeComplete;
    std::vector<std::string> messages;
    const int result = pump(*client, messages);
    if (!wasConnected && client->handshakeComplete)
      impl_->events.push_back({TlsEventType::Connected, id, {}});
    for (std::string& message : messages)
      impl_->events.push_back({TlsEventType::Message, id, std::move(message)});
    if (result != 0) { impl_->error = tlsError(result); remove.push_back(id); }
  }
  for (const std::uint32_t id : remove) disconnect(id);
}

bool TlsServer::send(const std::uint32_t clientId, const std::string& message) {
  const auto found = impl_->clients.find(clientId);
  if (found == impl_->clients.end() || message.size() > MaxFrame) return false;
  found->second->output.push_back(frame(message));
  return true;
}
void TlsServer::disconnect(const std::uint32_t clientId) {
  if (impl_->clients.erase(clientId) > 0) impl_->events.push_back({TlsEventType::Disconnected, clientId, {}});
}
std::vector<TlsEvent> TlsServer::drainEvents() { return std::exchange(impl_->events, {}); }
const std::string& TlsServer::lastError() const { return impl_->error; }
void TlsServer::stop() {
  impl_->clients.clear(); impl_->events.clear(); mbedtls_net_free(&impl_->listener);
  mbedtls_net_init(&impl_->listener);
}

struct TlsClient::Impl {
  TlsConfig config;
  std::unique_ptr<Connection> connection;
  std::vector<TlsEvent> events;
  std::string error;
  std::string serverName;
  bool tcpConnecting = false;
};
TlsClient::TlsClient() : impl_(std::make_unique<Impl>()) {}
TlsClient::~TlsClient() = default;

bool TlsClient::connect(const std::string& host, const std::uint16_t port,
                        const std::filesystem::path& trustedCertificate, const std::string& serverName) {
  disconnect();
  impl_->config.reset();
  if (!impl_->config.seed(impl_->error)) return false;
  int result = mbedtls_x509_crt_parse_file(&impl_->config.certificate, trustedCertificate.string().c_str());
  if (result == 0) result = mbedtls_ssl_config_defaults(&impl_->config.ssl, MBEDTLS_SSL_IS_CLIENT,
                                                        MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
  if (result != 0) { impl_->error = tlsError(result); return false; }
  mbedtls_ssl_conf_ca_chain(&impl_->config.ssl, &impl_->config.certificate, nullptr);
  mbedtls_ssl_conf_authmode(&impl_->config.ssl, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_rng(&impl_->config.ssl, mbedtls_ctr_drbg_random, &impl_->config.random);
  mbedtls_ssl_conf_min_version(&impl_->config.ssl, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
  mbedtls_ssl_conf_max_version(&impl_->config.ssl, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
  auto connection = std::make_unique<Connection>();
  int socketFd = -1;
  const int connectionState = beginNonBlockingConnect(host, port, socketFd, impl_->error);
  if (connectionState < 0) return false;
  connection->net.fd = socketFd;
  impl_->serverName = serverName;
  impl_->tcpConnecting = connectionState == 1;
  if (!impl_->tcpConnecting && !configureTlsClient(impl_->config, *connection, impl_->serverName, impl_->error)) return false;
  impl_->connection = std::move(connection);
  return true;
}
void TlsClient::update() {
  if (!impl_->connection) return;
  if (impl_->tcpConnecting) {
    std::string connectionError;
    if (!finishNonBlockingConnect(impl_->connection->net.fd, connectionError)) return;
    if (!connectionError.empty() || !configureTlsClient(impl_->config, *impl_->connection, impl_->serverName, impl_->error)) {
      if (!connectionError.empty()) impl_->error = std::move(connectionError);
      disconnect();
      return;
    }
    impl_->tcpConnecting = false;
  }
  const bool wasConnected = impl_->connection->handshakeComplete;
  std::vector<std::string> messages;
  const int result = pump(*impl_->connection, messages);
  if (!wasConnected && impl_->connection->handshakeComplete)
    impl_->events.push_back({TlsEventType::Connected, 0, {}});
  for (std::string& message : messages)
    impl_->events.push_back({TlsEventType::Message, 0, std::move(message)});
  if (result != 0) { impl_->error = tlsError(result); disconnect(); }
}
bool TlsClient::send(const std::string& message) {
  if (!impl_->connection || message.size() > MaxFrame) return false;
  impl_->connection->output.push_back(frame(message)); return true;
}
void TlsClient::disconnect() {
  if (impl_->connection) {
    impl_->connection.reset();
    impl_->tcpConnecting = false;
    impl_->events.push_back({TlsEventType::Disconnected, 0, {}});
  }
}
std::vector<TlsEvent> TlsClient::drainEvents() { return std::exchange(impl_->events, {}); }
const std::string& TlsClient::lastError() const { return impl_->error; }
bool TlsClient::isConnected() const { return impl_->connection && !impl_->tcpConnecting && impl_->connection->handshakeComplete; }

} // namespace demi::runtime
