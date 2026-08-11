#include "demi/runtime/network/NetworkSessionLifecycle.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <limits>
#include <random>
#include <sstream>

namespace demi::runtime {
namespace {

bool legalTransition(const NetworkSessionPhase from,
                     const NetworkSessionPhase to) {
  if (to == NetworkSessionPhase::Closed)
    return from != NetworkSessionPhase::Closed;
  switch (from) {
  case NetworkSessionPhase::Closed:
    return to == NetworkSessionPhase::Connected;
  case NetworkSessionPhase::Connected:
    return to == NetworkSessionPhase::Authenticated;
  case NetworkSessionPhase::Authenticated:
    return to == NetworkSessionPhase::Ready ||
           to == NetworkSessionPhase::Reconnecting;
  case NetworkSessionPhase::Ready:
    return to == NetworkSessionPhase::Active ||
           to == NetworkSessionPhase::Reconnecting;
  case NetworkSessionPhase::Active:
    return to == NetworkSessionPhase::Ready ||
           to == NetworkSessionPhase::Reconnecting;
  case NetworkSessionPhase::Reconnecting:
    return to == NetworkSessionPhase::Authenticated;
  }
  return false;
}

} // namespace

bool NetworkSessionLifecycle::transition(const NetworkSessionPhase next) {
  if (!legalTransition(phase_, next))
    return false;
  phase_ = next;
  return true;
}

void NetworkSessionLifecycle::reset() { phase_ = NetworkSessionPhase::Closed; }

NetworkSessionPhase NetworkSessionLifecycle::phase() const { return phase_; }

std::string ReconnectLeaseStore::issue(
    std::string peerId, const NetworkOwnershipRegistry &ownership,
    const std::uint64_t nowMillis, const std::uint64_t lifetimeMillis) {
  std::string token;
  do {
    token = randomToken();
  } while (leases_.contains(token));
  const std::uint64_t expiresAt =
      lifetimeMillis > std::numeric_limits<std::uint64_t>::max() - nowMillis
          ? std::numeric_limits<std::uint64_t>::max()
          : nowMillis + lifetimeMillis;
  ReconnectLease lease{.token = token,
                       .peerId = std::move(peerId),
                       .sessionEpoch = ownership.sessionEpoch(),
                       .expiresAtMillis = expiresAt,
                       .entities = {}};
  for (const NetworkOwnedEntity &entity : ownership.snapshot())
    if (entity.ownerPeerId == lease.peerId)
      lease.entities.push_back({.networkId = entity.networkId,
                                .ownershipGeneration =
                                    entity.ownershipGeneration});
  leases_.insert_or_assign(token, std::move(lease));
  return token;
}

ReconnectResult ReconnectLeaseStore::consume(
    const std::string_view token, const NetworkOwnershipRegistry &ownership,
    const std::uint64_t nowMillis) {
  const auto found = leases_.find(token);
  if (found == leases_.end())
    return {.accepted = false,
            .code = ReconnectRejectCode::UnknownToken,
            .peerId = {}};
  ReconnectLease lease = std::move(found->second);
  leases_.erase(found); // Tokens are single-use even when stale or expired.
  if (nowMillis > lease.expiresAtMillis)
    return {.accepted = false, .code = ReconnectRejectCode::Expired, .peerId = {}};
  if (lease.sessionEpoch != ownership.sessionEpoch())
    return {.accepted = false,
            .code = ReconnectRejectCode::StaleEpoch,
            .peerId = {}};
  for (const ReconnectEntityLease &expected : lease.entities) {
    const NetworkOwnedEntity *actual = ownership.find(expected.networkId);
    if (actual == nullptr || actual->ownerPeerId != lease.peerId ||
        actual->ownershipGeneration != expected.ownershipGeneration)
      return {.accepted = false,
              .code = ReconnectRejectCode::StaleOwnership,
              .peerId = {}};
  }
  return {.accepted = true,
          .code = ReconnectRejectCode::None,
          .peerId = std::move(lease.peerId)};
}

void ReconnectLeaseStore::revokePeer(const std::string_view peerId) {
  std::erase_if(leases_, [&](const auto &entry) {
    return entry.second.peerId == peerId;
  });
}

void ReconnectLeaseStore::reset() { leases_.clear(); }

std::size_t ReconnectLeaseStore::size() const { return leases_.size(); }

std::string ReconnectLeaseStore::randomToken() {
  std::random_device random;
  std::array<std::uint32_t, 4> words{};
  for (std::uint32_t &word : words)
    word = random();
  std::ostringstream token;
  token << std::hex << std::setfill('0');
  for (const std::uint32_t word : words)
    token << std::setw(8) << word;
  return token.str();
}

std::string_view networkSessionPhaseName(const NetworkSessionPhase phase) {
  switch (phase) {
  case NetworkSessionPhase::Closed: return "closed";
  case NetworkSessionPhase::Connected: return "connected";
  case NetworkSessionPhase::Authenticated: return "authenticated";
  case NetworkSessionPhase::Ready: return "ready";
  case NetworkSessionPhase::Active: return "active";
  case NetworkSessionPhase::Reconnecting: return "reconnecting";
  }
  return "closed";
}

std::string_view reconnectRejectCodeName(const ReconnectRejectCode code) {
  switch (code) {
  case ReconnectRejectCode::None: return "none";
  case ReconnectRejectCode::UnknownToken: return "unknown_token";
  case ReconnectRejectCode::Expired: return "expired";
  case ReconnectRejectCode::StaleEpoch: return "stale_epoch";
  case ReconnectRejectCode::StaleOwnership: return "stale_ownership";
  }
  return "unknown_token";
}

} // namespace demi::runtime
