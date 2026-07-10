#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace demi::runtime {

class DtlsTransport;

enum class NetworkMode {
  Offline,
  Host,
  Client,
};

enum class NetworkEventType {
  Connected,
  Disconnected,
  Message,
};

struct NetworkEvent {
  NetworkEventType type = NetworkEventType::Disconnected;
  std::uint32_t peerId = 0;
  std::uint8_t channel = 0;
  std::string message;
  std::uint32_t latencyMs = 0;
};

class NetworkSystem {
public:
  NetworkSystem();
  ~NetworkSystem();

  NetworkSystem(const NetworkSystem&) = delete;
  NetworkSystem& operator=(const NetworkSystem&) = delete;

  [[nodiscard]] bool initialize();
  [[nodiscard]] bool available() const;
  [[nodiscard]] bool host(std::uint16_t port, std::uint32_t maxPeers = 8, std::uint8_t channels = 2);
  [[nodiscard]] bool hostSecure(std::uint16_t port, const std::filesystem::path& certificate, const std::filesystem::path& privateKey,
                                std::uint32_t maxPeers = 8, std::uint8_t channels = 2);
  [[nodiscard]] bool connect(const std::string& address, std::uint16_t port, std::uint8_t channels = 2);
  [[nodiscard]] bool connectSecure(const std::string& address, std::uint16_t port, const std::filesystem::path& trustedCertificate,
                                   const std::string& serverName, std::uint8_t channels = 2);
  void disconnect();
  void disconnectPeer(std::uint32_t peerId);
  [[nodiscard]] bool send(const std::string& message, bool reliable = true, std::uint8_t channel = 0, std::uint32_t peerId = 0);
  void update(std::uint32_t maxEvents = 64);
  [[nodiscard]] std::vector<NetworkEvent> drainEvents();
  [[nodiscard]] NetworkMode mode() const;
  [[nodiscard]] bool isHosting() const;
  [[nodiscard]] bool isConnected() const;
  [[nodiscard]] std::uint32_t latencyMs() const;
  [[nodiscard]] bool isSecure() const;
  [[nodiscard]] const std::string& securityError() const;
  void shutdown();

private:
  [[nodiscard]] bool createHost(std::uint16_t port, std::uint32_t maxPeers, std::uint8_t channels);
  [[nodiscard]] bool createClient(const std::string& address, std::uint16_t port, std::uint8_t channels);
  void sendDtlsDatagrams(std::uint32_t peerId);
  [[nodiscard]] bool sendRaw(const std::string& message, bool reliable, std::uint8_t channel, std::uint32_t peerId);

  std::unique_ptr<DtlsTransport> dtls_;
  void* host_ = nullptr;
  void* serverPeer_ = nullptr;
  std::unordered_map<std::uint32_t, void*> peers_;
  std::vector<NetworkEvent> events_;
  NetworkMode mode_ = NetworkMode::Offline;
  std::uint32_t nextPeerId_ = 1;
  bool initialized_ = false;
  bool connected_ = false;
};

} // namespace demi::runtime
