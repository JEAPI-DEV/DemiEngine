#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <string_view>

namespace demi::runtime {

struct Entity;
struct NetworkContract;
enum class NetworkActor;

struct ReplicatedStateResult {
  bool ok = false;
  std::string error;
};

// Serializes and applies only fields explicitly marked as replicated in the
// component registry. Transport and scripting code must not bypass this gate.
[[nodiscard]] nlohmann::json captureReplicatedState(const Entity &entity);
[[nodiscard]] nlohmann::json captureContractReplicatedState(
    const Entity &entity, const NetworkContract &contract,
    std::string_view prefabKey, NetworkActor writer);
[[nodiscard]] ReplicatedStateResult
validateReplicatedState(const nlohmann::json &state);
[[nodiscard]] ReplicatedStateResult validateContractReplicatedState(
    const NetworkContract &contract, std::string_view prefabKey,
    NetworkActor writer, const nlohmann::json &state);
[[nodiscard]] ReplicatedStateResult
applyReplicatedState(Entity &entity, const nlohmann::json &state);
[[nodiscard]] bool isReplicatedField(std::string_view component,
                                     std::string_view field);

} // namespace demi::runtime
