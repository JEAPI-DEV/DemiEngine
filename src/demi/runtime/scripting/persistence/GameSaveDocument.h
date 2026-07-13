#pragma once

#include <optional>
#include <string>

#include <nlohmann/json_fwd.hpp>

namespace demi::runtime::persistence {

inline constexpr int CurrentGameSaveVersion = 2;

struct GameSaveMetadata {
  bool autosave = false;
  int sequence = 0;
  std::string reason;
};

[[nodiscard]] bool validateGameState(const nlohmann::json &state,
                                     std::string &error);
[[nodiscard]] std::optional<nlohmann::json>
buildGameSaveDocument(std::string slot, const nlohmann::json &state,
                      int formatVersion, const GameSaveMetadata &metadata,
                      std::string &error);
[[nodiscard]] bool validateGameSaveDocument(const nlohmann::json &document,
                                            std::string &error);

} // namespace demi::runtime::persistence
