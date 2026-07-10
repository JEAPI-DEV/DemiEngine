#include "demi/runtime/network/DtlsTransport.h"

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/pk.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_cookie.h>
#include <mbedtls/timing.h>
#include <mbedtls/x509_crt.h>

#include <algorithm>
#include <array>
#include <deque>
#include <unordered_map>
#include <utility>

namespace demi::runtime {

namespace {

constexpr std::size_t MaximumPlaintextSize = 64 * 1024;

std::string tlsError(const std::string& operation, const int code) {
  std::array<char, 256> text{};
  mbedtls_strerror(code, text.data(), text.size());
  return operation + " (" + std::to_string(code) + "): " + text.data();
}

struct DtlsPeer {
  mbedtls_ssl_context ssl{};
  mbedtls_timing_delay_context timer{};
  std::deque<std::string> incoming;
  std::vector<std::string> outgoing;
  std::vector<std::string> plaintext;
  bool ready = false;
  bool failed = false;

  DtlsPeer() {
    mbedtls_ssl_init(&ssl);
  }

  ~DtlsPeer() {
    mbedtls_ssl_free(&ssl);
  }
};

std::array<unsigned char, 4> peerTransportId(const std::uint32_t peerId) {
  return {
    static_cast<unsigned char>((peerId >> 24U) & 0xffU),
    static_cast<unsigned char>((peerId >> 16U) & 0xffU),
    static_cast<unsigned char>((peerId >> 8U) & 0xffU),
    static_cast<unsigned char>(peerId & 0xffU),
  };
}

int sendDatagramCallback(void* context, const unsigned char* data, const std::size_t size) {
  auto& peer = *static_cast<DtlsPeer*>(context);
  peer.outgoing.emplace_back(reinterpret_cast<const char*>(data), size);
  return static_cast<int>(size);
}

int receiveDatagramCallback(void* context, unsigned char* data, const std::size_t size) {
  auto& peer = *static_cast<DtlsPeer*>(context);
  if (peer.incoming.empty()) {
    return MBEDTLS_ERR_SSL_WANT_READ;
  }
  if (peer.incoming.front().size() > size) {
    return MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL;
  }
  const std::string datagram = std::move(peer.incoming.front());
  peer.incoming.pop_front();
  std::copy(datagram.begin(), datagram.end(), data);
  return static_cast<int>(datagram.size());
}

} // namespace

struct DtlsTransport::Impl {
  enum class Mode {
    Disabled,
    Server,
    Client,
  };

  mbedtls_entropy_context entropy{};
  mbedtls_ctr_drbg_context random{};
  mbedtls_ssl_config config{};
  mbedtls_ssl_cookie_ctx cookies{};
  mbedtls_x509_crt certificate{};
  mbedtls_pk_context privateKey{};
  std::unordered_map<std::uint32_t, std::unique_ptr<DtlsPeer>> peers;
  std::vector<std::uint32_t> readyPeers;
  std::vector<std::uint32_t> failedPeers;
  std::string serverName;
  std::string error;
  Mode mode = Mode::Disabled;

  Impl() {
    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&random);
    mbedtls_ssl_config_init(&config);
    mbedtls_ssl_cookie_init(&cookies);
    mbedtls_x509_crt_init(&certificate);
    mbedtls_pk_init(&privateKey);
  }

  ~Impl() {
    clear();
    mbedtls_pk_free(&privateKey);
    mbedtls_x509_crt_free(&certificate);
    mbedtls_ssl_config_free(&config);
    mbedtls_ssl_cookie_free(&cookies);
    mbedtls_ctr_drbg_free(&random);
    mbedtls_entropy_free(&entropy);
  }

  void clear() {
    peers.clear();
    readyPeers.clear();
    failedPeers.clear();
    serverName.clear();
    error.clear();
    mode = Mode::Disabled;
    mbedtls_pk_free(&privateKey);
    mbedtls_x509_crt_free(&certificate);
    mbedtls_ssl_config_free(&config);
    mbedtls_ssl_cookie_free(&cookies);
    mbedtls_ctr_drbg_free(&random);
    mbedtls_pk_init(&privateKey);
    mbedtls_x509_crt_init(&certificate);
    mbedtls_ssl_config_init(&config);
    mbedtls_ssl_cookie_init(&cookies);
    mbedtls_ctr_drbg_init(&random);
  }

  bool initializeConfig(const int endpoint) {
    static constexpr unsigned char Personalization[] = "DemiEngine DTLS";
    int result = mbedtls_ctr_drbg_seed(&random, mbedtls_entropy_func, &entropy, Personalization, sizeof(Personalization) - 1);
    if (result != 0) {
      error = tlsError("DTLS random initialization failed", result);
      return false;
    }
    result = mbedtls_ssl_config_defaults(&config, endpoint, MBEDTLS_SSL_TRANSPORT_DATAGRAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (result != 0) {
      error = tlsError("DTLS configuration failed", result);
      return false;
    }
    mbedtls_ssl_conf_rng(&config, mbedtls_ctr_drbg_random, &random);
    mbedtls_ssl_conf_min_version(&config, MBEDTLS_SSL_MAJOR_VERSION_3, MBEDTLS_SSL_MINOR_VERSION_3);
    return true;
  }

  void failPeer(const std::uint32_t peerId, DtlsPeer& peer, const int result) {
    if (peer.failed) {
      return;
    }
    peer.failed = true;
    failedPeers.push_back(peerId);
    error = tlsError("DTLS peer failed", result);
  }

  void advancePeer(const std::uint32_t peerId, DtlsPeer& peer) {
    if (peer.failed) {
      return;
    }
    if (!peer.ready) {
      const int result = mbedtls_ssl_handshake(&peer.ssl);
      if (result == 0) {
        peer.ready = true;
        readyPeers.push_back(peerId);
      } else if (result == MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED && mode == Mode::Server) {
        const int resetResult = mbedtls_ssl_session_reset(&peer.ssl);
        const auto transportId = peerTransportId(peerId);
        const int identityResult = resetResult == 0
          ? mbedtls_ssl_set_client_transport_id(&peer.ssl, transportId.data(), transportId.size())
          : resetResult;
        if (identityResult != 0) {
          failPeer(peerId, peer, identityResult);
        }
      } else if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
        failPeer(peerId, peer, result);
      }
      return;
    }

    std::array<unsigned char, MaximumPlaintextSize> buffer{};
    while (true) {
      const int result = mbedtls_ssl_read(&peer.ssl, buffer.data(), buffer.size());
      if (result > 0) {
        peer.plaintext.emplace_back(reinterpret_cast<const char*>(buffer.data()), static_cast<std::size_t>(result));
      } else if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return;
      } else if (result == 0 || result == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        failPeer(peerId, peer, result);
        return;
      } else {
        failPeer(peerId, peer, result);
        return;
      }
    }
  }
};

DtlsTransport::DtlsTransport() : impl_(std::make_unique<Impl>()) {}

DtlsTransport::~DtlsTransport() = default;

bool DtlsTransport::configureServer(const std::filesystem::path& certificate, const std::filesystem::path& privateKey) {
  impl_->clear();
  if (!impl_->initializeConfig(MBEDTLS_SSL_IS_SERVER)) {
    return false;
  }
  int result = mbedtls_x509_crt_parse_file(&impl_->certificate, certificate.string().c_str());
  if (result != 0) {
    impl_->error = tlsError("DTLS certificate could not be loaded", result);
    return false;
  }
  result = mbedtls_pk_parse_keyfile(&impl_->privateKey, privateKey.string().c_str(), nullptr, mbedtls_ctr_drbg_random, &impl_->random);
  if (result != 0) {
    impl_->error = tlsError("DTLS private key could not be loaded", result);
    return false;
  }
  result = mbedtls_ssl_conf_own_cert(&impl_->config, &impl_->certificate, &impl_->privateKey);
  if (result != 0) {
    impl_->error = tlsError("DTLS certificate and private key do not match", result);
    return false;
  }
  mbedtls_ssl_conf_authmode(&impl_->config, MBEDTLS_SSL_VERIFY_NONE);
  result = mbedtls_ssl_cookie_setup(&impl_->cookies, mbedtls_ctr_drbg_random, &impl_->random);
  if (result != 0) {
    impl_->error = tlsError("DTLS cookie setup failed", result);
    return false;
  }
  mbedtls_ssl_conf_dtls_cookies(&impl_->config, mbedtls_ssl_cookie_write, mbedtls_ssl_cookie_check, &impl_->cookies);
  impl_->mode = Impl::Mode::Server;
  return true;
}

bool DtlsTransport::configureClient(const std::filesystem::path& trustedCertificate, const std::string& serverName) {
  impl_->clear();
  if (!impl_->initializeConfig(MBEDTLS_SSL_IS_CLIENT)) {
    return false;
  }
  const int result = mbedtls_x509_crt_parse_file(&impl_->certificate, trustedCertificate.string().c_str());
  if (result != 0) {
    impl_->error = tlsError("DTLS trusted certificate could not be loaded", result);
    return false;
  }
  mbedtls_ssl_conf_ca_chain(&impl_->config, &impl_->certificate, nullptr);
  mbedtls_ssl_conf_authmode(&impl_->config, MBEDTLS_SSL_VERIFY_REQUIRED);
  impl_->serverName = serverName;
  impl_->mode = Impl::Mode::Client;
  return true;
}

void DtlsTransport::reset() {
  impl_->clear();
}

bool DtlsTransport::addPeer(const std::uint32_t peerId) {
  if (impl_->mode == Impl::Mode::Disabled || impl_->peers.contains(peerId)) {
    return false;
  }
  auto peer = std::make_unique<DtlsPeer>();
  int result = mbedtls_ssl_setup(&peer->ssl, &impl_->config);
  if (result != 0) {
    impl_->error = tlsError("DTLS peer setup failed", result);
    return false;
  }
  if (impl_->mode == Impl::Mode::Client && !impl_->serverName.empty()) {
    result = mbedtls_ssl_set_hostname(&peer->ssl, impl_->serverName.c_str());
    if (result != 0) {
      impl_->error = tlsError("DTLS server name setup failed", result);
      return false;
    }
  }
  if (impl_->mode == Impl::Mode::Server) {
    const auto transportId = peerTransportId(peerId);
    result = mbedtls_ssl_set_client_transport_id(&peer->ssl, transportId.data(), transportId.size());
    if (result != 0) {
      impl_->error = tlsError("DTLS peer identity setup failed", result);
      return false;
    }
  }
  mbedtls_ssl_set_bio(&peer->ssl, peer.get(), sendDatagramCallback, receiveDatagramCallback, nullptr);
  mbedtls_ssl_set_timer_cb(&peer->ssl, &peer->timer, mbedtls_timing_set_delay, mbedtls_timing_get_delay);
  mbedtls_ssl_set_mtu(&peer->ssl, 1200);
  auto [position, inserted] = impl_->peers.emplace(peerId, std::move(peer));
  if (inserted) {
    impl_->advancePeer(peerId, *position->second);
  }
  return inserted;
}

void DtlsTransport::removePeer(const std::uint32_t peerId) {
  impl_->peers.erase(peerId);
}

bool DtlsTransport::receiveDatagram(const std::uint32_t peerId, const std::string& datagram) {
  const auto found = impl_->peers.find(peerId);
  if (found == impl_->peers.end() || found->second->failed || datagram.empty()) {
    return false;
  }
  found->second->incoming.push_back(datagram);
  impl_->advancePeer(peerId, *found->second);
  return !found->second->failed;
}

void DtlsTransport::update() {
  for (auto& [peerId, peer] : impl_->peers) {
    impl_->advancePeer(peerId, *peer);
  }
}

bool DtlsTransport::isEnabled() const {
  return impl_->mode != Impl::Mode::Disabled;
}

bool DtlsTransport::isReady(const std::uint32_t peerId) const {
  const auto found = impl_->peers.find(peerId);
  return found != impl_->peers.end() && found->second->ready && !found->second->failed;
}

std::vector<std::uint32_t> DtlsTransport::takeReadyPeers() {
  return std::exchange(impl_->readyPeers, {});
}

std::vector<std::uint32_t> DtlsTransport::takeFailedPeers() {
  return std::exchange(impl_->failedPeers, {});
}

bool DtlsTransport::encrypt(const std::uint32_t peerId, const std::string& plaintext) {
  const auto found = impl_->peers.find(peerId);
  if (found == impl_->peers.end() || !found->second->ready || found->second->failed || plaintext.empty()) {
    return false;
  }
  const int result = mbedtls_ssl_write(&found->second->ssl, reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size());
  if (result != static_cast<int>(plaintext.size())) {
    if (result != MBEDTLS_ERR_SSL_WANT_READ && result != MBEDTLS_ERR_SSL_WANT_WRITE) {
      impl_->failPeer(peerId, *found->second, result);
    }
    return false;
  }
  return true;
}

std::vector<std::string> DtlsTransport::takePlaintext(const std::uint32_t peerId) {
  const auto found = impl_->peers.find(peerId);
  if (found == impl_->peers.end()) {
    return {};
  }
  return std::exchange(found->second->plaintext, {});
}

std::vector<std::string> DtlsTransport::takeDatagrams(const std::uint32_t peerId) {
  const auto found = impl_->peers.find(peerId);
  if (found == impl_->peers.end()) {
    return {};
  }
  return std::exchange(found->second->outgoing, {});
}

const std::string& DtlsTransport::lastError() const {
  return impl_->error;
}

} // namespace demi::runtime
