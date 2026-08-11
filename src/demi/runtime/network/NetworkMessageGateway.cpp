#include "demi/runtime/network/NetworkMessageGateway.h"

#include "demi/runtime/network/NetworkOwnershipRegistry.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace demi::runtime {
namespace {

constexpr std::uint8_t Magic[] = {'D', 'N', 'E', 'T'};

std::uint64_t parseHash(const std::string &hash) {
  const std::size_t colon = hash.find(':');
  if (colon == std::string::npos)
    return 0;
  try {
    return std::stoull(hash.substr(colon + 1), nullptr, 16);
  } catch (...) {
    return 0;
  }
}

template <typename Integer>
void appendInteger(std::vector<std::uint8_t> &bytes, Integer value) {
  std::uint64_t remaining = static_cast<std::uint64_t>(value);
  for (std::size_t index = 0; index < sizeof(Integer); ++index) {
    bytes.push_back(static_cast<std::uint8_t>(remaining & 0xffU));
    remaining >>= 8U;
  }
}

template <typename Integer>
Integer readInteger(const std::span<const std::uint8_t> bytes,
                    std::size_t &cursor) {
  Integer value = 0;
  for (std::size_t index = 0; index < sizeof(Integer); ++index)
    value |= static_cast<Integer>(bytes[cursor++]) << (index * 8U);
  return value;
}

NetworkGatewayRejectCode validatePayload(const nlohmann::json &value,
                                         const NetworkContractLimits &limits,
                                         std::uint32_t depth,
                                         std::uint64_t &elements) {
  if (depth > limits.maximumPayloadDepth)
    return NetworkGatewayRejectCode::ExcessiveDepth;
  if (++elements > limits.maximumPayloadElements)
    return NetworkGatewayRejectCode::ExcessiveElements;
  if (value.is_string() &&
      value.get_ref<const std::string &>().size() > limits.maximumStringBytes)
    return NetworkGatewayRejectCode::StringTooLong;
  if (value.is_number_float() && !std::isfinite(value.get<double>()))
    return NetworkGatewayRejectCode::NonFiniteNumber;
  if (value.is_object()) {
    for (const auto &[key, child] : value.items()) {
      if (key.size() > limits.maximumStringBytes)
        return NetworkGatewayRejectCode::StringTooLong;
      if (const auto result =
              validatePayload(child, limits, depth + 1, elements);
          result != NetworkGatewayRejectCode::None)
        return result;
    }
  } else if (value.is_array()) {
    for (const auto &child : value)
      if (const auto result =
              validatePayload(child, limits, depth + 1, elements);
          result != NetworkGatewayRejectCode::None)
        return result;
  } else if (value.is_binary() || value.is_discarded()) {
    return NetworkGatewayRejectCode::InvalidJson;
  }
  return NetworkGatewayRejectCode::None;
}

bool permittedSender(const NetworkMessageRule &rule,
                     const NetworkGatewayContext &context,
                     const std::string_view target) {
  if (!context.authoritativeServer)
    return context.trustedSenderPeerId == "server";
  if (context.trustedSenderPeerId == "server")
    return rule.from == NetworkActor::Server || rule.from == NetworkActor::All;
  if (rule.from == NetworkActor::All)
    return true;
  return rule.from == NetworkActor::Owner && context.ownership != nullptr &&
         context.ownership->isOwner(target, context.trustedSenderPeerId);
}

bool matchesSchemaType(const nlohmann::json &value,
                       const std::string_view type) {
  if (type == "null")
    return value.is_null();
  if (type == "boolean")
    return value.is_boolean();
  if (type == "integer")
    return value.is_number_integer();
  if (type == "number")
    return value.is_number();
  if (type == "string")
    return value.is_string();
  if (type == "array")
    return value.is_array();
  if (type == "object")
    return value.is_object();
  return false;
}

bool conformsToSchema(const nlohmann::json &value,
                      const nlohmann::json &schema) {
  if (!schema.is_object())
    return false;
  if (const auto type = schema.find("type");
      type != schema.end() &&
      (!type->is_string() ||
       !matchesSchemaType(value, type->get<std::string>())))
    return false;
  if (const auto choices = schema.find("enum");
      choices != schema.end() &&
      (!choices->is_array() ||
       std::find(choices->begin(), choices->end(), value) == choices->end()))
    return false;
  if (value.is_number()) {
    const double number = value.get<double>();
    if ((schema.contains("minimum") && schema["minimum"].is_number() &&
         number < schema["minimum"].get<double>()) ||
        (schema.contains("maximum") && schema["maximum"].is_number() &&
         number > schema["maximum"].get<double>()))
      return false;
  }
  if (value.is_string() && schema.contains("maxLength") &&
      schema["maxLength"].is_number_unsigned() &&
      value.get_ref<const std::string &>().size() >
          schema["maxLength"].get<std::size_t>())
    return false;
  if (value.is_array()) {
    if (schema.contains("maxItems") &&
        schema["maxItems"].is_number_unsigned() &&
        value.size() > schema["maxItems"].get<std::size_t>())
      return false;
    if (const auto items = schema.find("items"); items != schema.end())
      for (const auto &child : value)
        if (!conformsToSchema(child, *items))
          return false;
  }
  if (value.is_object()) {
    if (const auto required = schema.find("required");
        required != schema.end()) {
      if (!required->is_array())
        return false;
      for (const auto &key : *required)
        if (!key.is_string() || !value.contains(key.get<std::string>()))
          return false;
    }
    const auto properties = schema.find("properties");
    if (properties != schema.end() && !properties->is_object())
      return false;
    if (properties != schema.end())
      for (const auto &[key, childSchema] : properties->items())
        if (value.contains(key) && !conformsToSchema(value[key], childSchema))
          return false;
    const auto additional = schema.find("additionalProperties");
    if (additional != schema.end() && !additional->is_boolean())
      return false;
    if (additional != schema.end() && additional->get<bool>() == false) {
      if (properties == schema.end())
        return value.empty();
      for (const auto &[key, unused] : value.items()) {
        (void)unused;
        if (!properties->contains(key))
          return false;
      }
    }
  }
  return true;
}

} // namespace

std::vector<std::uint8_t>
NetworkMessageGateway::encode(const NetworkContract &contract,
                              const NetworkEnvelope &envelope) const {
  const nlohmann::json payload = {{"name", envelope.name},
                                  {"target", envelope.target},
                                  {"data", envelope.data}};
  const std::string json = payload.dump();
  std::vector<std::uint8_t> bytes;
  bytes.reserve(HeaderBytes + json.size());
  bytes.insert(bytes.end(), std::begin(Magic), std::end(Magic));
  appendInteger(bytes, ProtocolVersion);
  appendInteger(bytes, static_cast<std::uint8_t>(envelope.kind));
  appendInteger(bytes, static_cast<std::uint8_t>(0));
  appendInteger(bytes, envelope.sessionEpoch);
  appendInteger(bytes, envelope.ownershipGeneration);
  appendInteger(bytes, envelope.sequence);
  appendInteger(bytes, parseHash(contract.compatibilityHash));
  appendInteger(bytes, static_cast<std::uint32_t>(json.size()));
  bytes.insert(bytes.end(), json.begin(), json.end());
  return bytes;
}

NetworkGatewayResult
NetworkMessageGateway::accept(const std::span<const std::uint8_t> bytes,
                              const NetworkGatewayContext &context) {
  if (context.contract == nullptr)
    return reject(NetworkGatewayRejectCode::InvalidOperation,
                  "message gateway has no active contract");
  const NetworkContract &contract = *context.contract;
  if (bytes.size() < HeaderBytes)
    return reject(NetworkGatewayRejectCode::Truncated,
                  "network envelope is shorter than its fixed header");
  if (!std::equal(std::begin(Magic), std::end(Magic), bytes.begin()))
    return reject(NetworkGatewayRejectCode::BadMagic,
                  "network envelope magic does not match");
  std::size_t cursor = 4;
  const auto protocol = readInteger<std::uint16_t>(bytes, cursor);
  const auto kindValue = readInteger<std::uint8_t>(bytes, cursor);
  (void)readInteger<std::uint8_t>(bytes, cursor);
  const auto epoch = readInteger<std::uint64_t>(bytes, cursor);
  const auto generation = readInteger<std::uint64_t>(bytes, cursor);
  const auto sequence = readInteger<std::uint64_t>(bytes, cursor);
  const auto contractHash = readInteger<std::uint64_t>(bytes, cursor);
  const auto payloadBytes = readInteger<std::uint32_t>(bytes, cursor);
  const auto kind = static_cast<NetworkEnvelopeKind>(kindValue);
  if (protocol != ProtocolVersion)
    return reject(NetworkGatewayRejectCode::UnsupportedProtocol,
                  "unsupported network protocol version");
  if (contractHash != parseHash(contract.compatibilityHash))
    return reject(NetworkGatewayRejectCode::ContractMismatch,
                  "network contract compatibility hash differs");
  const bool authoritativeHandshake =
      kind == NetworkEnvelopeKind::Session && !context.authoritativeServer &&
      context.trustedSenderPeerId == "server" && epoch != 0;
  if (context.ownership == nullptr ||
      (!authoritativeHandshake && epoch != context.ownership->sessionEpoch()))
    return reject(NetworkGatewayRejectCode::StaleEpoch,
                  "network envelope belongs to another session epoch");
  if (payloadBytes > contract.limits.maximumMessageBytes ||
      bytes.size() != HeaderBytes + payloadBytes)
    return reject(payloadBytes > contract.limits.maximumMessageBytes
                      ? NetworkGatewayRejectCode::Oversized
                      : NetworkGatewayRejectCode::Truncated,
                  "network payload length is invalid");

  const std::string sequenceKey = context.trustedSenderPeerId;
  if (sequence == 0 || (lastSequences_.contains(sequenceKey) &&
                        sequence <= lastSequences_.at(sequenceKey)))
    return reject(NetworkGatewayRejectCode::Replay,
                  "network sequence was duplicated or reordered");

  nlohmann::json payload;
  try {
    payload = nlohmann::json::parse(bytes.begin() + HeaderBytes, bytes.end());
  } catch (const nlohmann::json::exception &) {
    return reject(NetworkGatewayRejectCode::InvalidJson,
                  "network payload is not valid JSON");
  }
  std::uint64_t elements = 0;
  if (const auto validation =
          validatePayload(payload, contract.limits, 1, elements);
      validation != NetworkGatewayRejectCode::None)
    return reject(validation, "network payload exceeded structural limits");
  if (!payload.is_object() || !payload.contains("name") ||
      !payload["name"].is_string() || !payload.contains("data"))
    return reject(NetworkGatewayRejectCode::InvalidJson,
                  "network payload is missing name or data");

  NetworkEnvelope envelope;
  envelope.kind = kind;
  envelope.sessionEpoch = epoch;
  envelope.ownershipGeneration = generation;
  envelope.sequence = sequence;
  envelope.name = payload["name"].get<std::string>();
  envelope.target = payload.value("target", "");
  envelope.data = payload["data"];

  if (envelope.kind != NetworkEnvelopeKind::Message) {
    if (!context.authoritativeServer &&
        context.trustedSenderPeerId == "server") {
      const NetworkOwnedEntity *entity =
          context.ownership == nullptr
              ? nullptr
              : context.ownership->find(envelope.target);
      if (envelope.kind == NetworkEnvelopeKind::Spawn &&
          (envelope.ownershipGeneration == 0 || entity != nullptr))
        return reject(NetworkGatewayRejectCode::StaleGeneration,
                      "spawn generation is invalid or entity already exists");
      if ((envelope.kind == NetworkEnvelopeKind::Ownership ||
           envelope.kind == NetworkEnvelopeKind::Despawn) &&
          (entity == nullptr ||
           envelope.ownershipGeneration <= entity->ownershipGeneration))
        return reject(NetworkGatewayRejectCode::StaleGeneration,
                      "lifecycle operation uses a stale generation");
      lastSequences_[sequenceKey] = sequence;
      ++counters_.accepted;
      return {.accepted = true,
              .code = NetworkGatewayRejectCode::None,
              .internalReason = {},
              .envelope = std::move(envelope)};
    }
    return reject(NetworkGatewayRejectCode::UnauthorizedSender,
                  "only the server may issue lifecycle operations");
  }
  const auto message = contract.messages.find(envelope.name);
  if (message == contract.messages.end())
    return reject(NetworkGatewayRejectCode::UnknownMessage,
                  "message is not declared by the active contract");
  if (payloadBytes > message->second.maximumBytes)
    return reject(NetworkGatewayRejectCode::Oversized,
                  "message exceeds its declared byte limit");
  if (context.authoritativeServer &&
      message->second.to != NetworkActor::Server &&
      message->second.to != NetworkActor::All)
    return reject(NetworkGatewayRejectCode::UnauthorizedTarget,
                  "message is not addressed to the server");
  if (!context.authoritativeServer && message->second.to != NetworkActor::All &&
      !(message->second.to == NetworkActor::Owner &&
        context.ownership != nullptr &&
        context.ownership->isOwner(envelope.target, context.localPeerId)))
    return reject(NetworkGatewayRejectCode::UnauthorizedTarget,
                  "message is not visible to this client");
  if (!envelope.target.empty() && context.ownership != nullptr) {
    if (const NetworkOwnedEntity *entity =
            context.ownership->find(envelope.target);
        entity != nullptr &&
        envelope.ownershipGeneration != entity->ownershipGeneration)
      return reject(NetworkGatewayRejectCode::StaleGeneration,
                    "message uses a stale ownership generation");
  }
  if (message->second.target == "owned_entity" &&
      (envelope.target.empty() || context.ownership == nullptr ||
       !context.ownership->isOwner(envelope.target,
                                   context.trustedSenderPeerId)))
    return reject(NetworkGatewayRejectCode::UnauthorizedTarget,
                  "message target is not owned by the authenticated peer");
  if (message->second.target == "entity" &&
      (context.ownership == nullptr ||
       context.ownership->find(envelope.target) == nullptr))
    return reject(NetworkGatewayRejectCode::UnauthorizedTarget,
                  "message target does not exist");
  if (!permittedSender(message->second, context, envelope.target))
    return reject(NetworkGatewayRejectCode::UnauthorizedSender,
                  "authenticated peer cannot send this message");
  if (!message->second.schema.empty() &&
      (message->second.schemaDocument.empty() ||
       !conformsToSchema(envelope.data, message->second.schemaDocument)))
    return reject(NetworkGatewayRejectCode::SchemaViolation,
                  "message data does not conform to its declared schema");

  const std::string rateKey =
      context.trustedSenderPeerId + "\n" + envelope.name;
  RateWindow &window = rateWindows_[rateKey];
  while (!window.acceptedAt.empty() &&
         context.nowSeconds - window.acceptedAt.front() >= 1.0)
    window.acceptedAt.pop_front();
  const std::uint32_t limit = std::min(
      message->second.rateLimit, contract.limits.maximumMessagesPerSecond);
  if (window.acceptedAt.size() >= limit)
    return reject(NetworkGatewayRejectCode::RateLimited,
                  "message exceeded its declared rate limit");

  window.acceptedAt.push_back(context.nowSeconds);
  lastSequences_[sequenceKey] = sequence;
  ++counters_.accepted;
  return {.accepted = true,
          .code = NetworkGatewayRejectCode::None,
          .internalReason = {},
          .envelope = std::move(envelope)};
}

void NetworkMessageGateway::reset() {
  lastSequences_.clear();
  rateWindows_.clear();
  counters_ = {};
}

const NetworkGatewayCounters &NetworkMessageGateway::counters() const {
  return counters_;
}

NetworkGatewayResult
NetworkMessageGateway::reject(const NetworkGatewayRejectCode code,
                              std::string reason) {
  ++counters_.rejected[code];
  return {.accepted = false,
          .code = code,
          .internalReason = std::move(reason),
          .envelope = std::nullopt};
}

std::string_view
networkGatewayRejectCodeName(const NetworkGatewayRejectCode code) {
  switch (code) {
  case NetworkGatewayRejectCode::None:
    return "none";
  case NetworkGatewayRejectCode::Truncated:
    return "truncated";
  case NetworkGatewayRejectCode::BadMagic:
    return "bad_magic";
  case NetworkGatewayRejectCode::UnsupportedProtocol:
    return "protocol";
  case NetworkGatewayRejectCode::ContractMismatch:
    return "contract_mismatch";
  case NetworkGatewayRejectCode::StaleEpoch:
    return "stale_epoch";
  case NetworkGatewayRejectCode::StaleGeneration:
    return "stale_generation";
  case NetworkGatewayRejectCode::Replay:
    return "replay";
  case NetworkGatewayRejectCode::Oversized:
    return "oversized";
  case NetworkGatewayRejectCode::InvalidJson:
    return "invalid_json";
  case NetworkGatewayRejectCode::ExcessiveDepth:
    return "excessive_depth";
  case NetworkGatewayRejectCode::ExcessiveElements:
    return "excessive_elements";
  case NetworkGatewayRejectCode::StringTooLong:
    return "string_too_long";
  case NetworkGatewayRejectCode::NonFiniteNumber:
    return "non_finite";
  case NetworkGatewayRejectCode::SchemaViolation:
    return "schema_violation";
  case NetworkGatewayRejectCode::UnknownMessage:
    return "unknown_message";
  case NetworkGatewayRejectCode::UnauthorizedSender:
    return "unauthorized_sender";
  case NetworkGatewayRejectCode::UnauthorizedTarget:
    return "unauthorized_target";
  case NetworkGatewayRejectCode::RateLimited:
    return "rate_limited";
  case NetworkGatewayRejectCode::InvalidOperation:
    return "invalid_operation";
  }
  return "unknown";
}

} // namespace demi::runtime
