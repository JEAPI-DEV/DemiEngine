#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace demi::runtime {

enum class TlsEventType { Connected, Disconnected, Message };

struct TlsEvent {
  TlsEventType type = TlsEventType::Disconnected;
  std::uint32_t clientId = 0;
  std::string message;
};

class TlsServer {
public:
  TlsServer();
  ~TlsServer();
  [[nodiscard]] bool listen(std::uint16_t port, const std::filesystem::path& certificate,
                            const std::filesystem::path& privateKey, std::uint32_t maxClients = 64);
  void update();
  [[nodiscard]] bool send(std::uint32_t clientId, const std::string& message);
  void disconnect(std::uint32_t clientId);
  [[nodiscard]] std::vector<TlsEvent> drainEvents();
  [[nodiscard]] const std::string& lastError() const;
  void stop();
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

class TlsClient {
public:
  TlsClient();
  ~TlsClient();
  [[nodiscard]] bool connect(const std::string& host, std::uint16_t port,
                             const std::filesystem::path& trustedCertificate,
                             const std::string& serverName);
  void update();
  [[nodiscard]] bool send(const std::string& message);
  void disconnect();
  [[nodiscard]] std::vector<TlsEvent> drainEvents();
  [[nodiscard]] const std::string& lastError() const;
  [[nodiscard]] bool isConnected() const;
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace demi::runtime
