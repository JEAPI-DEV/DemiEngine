#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace demi::runtime {

class DtlsTransport {
public:
  DtlsTransport();
  ~DtlsTransport();

  DtlsTransport(const DtlsTransport&) = delete;
  DtlsTransport& operator=(const DtlsTransport&) = delete;

  [[nodiscard]] bool configureServer(const std::filesystem::path& certificate, const std::filesystem::path& privateKey);
  [[nodiscard]] bool configureClient(const std::filesystem::path& trustedCertificate, const std::string& serverName);
  void reset();

  [[nodiscard]] bool addPeer(std::uint32_t peerId);
  void removePeer(std::uint32_t peerId);
  [[nodiscard]] bool receiveDatagram(std::uint32_t peerId, const std::string& datagram);
  void update();

  [[nodiscard]] bool isEnabled() const;
  [[nodiscard]] bool isReady(std::uint32_t peerId) const;
  [[nodiscard]] std::vector<std::uint32_t> takeReadyPeers();
  [[nodiscard]] std::vector<std::uint32_t> takeFailedPeers();
  [[nodiscard]] bool encrypt(std::uint32_t peerId, const std::string& plaintext);
  [[nodiscard]] std::vector<std::string> takePlaintext(std::uint32_t peerId);
  [[nodiscard]] std::vector<std::string> takeDatagrams(std::uint32_t peerId);
  [[nodiscard]] const std::string& lastError() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace demi::runtime
