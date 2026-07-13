#include "demi/runtime/scripting/persistence/GameSaveDocument.h"

#include <nlohmann/json.hpp>

namespace demi::runtime::persistence {

bool validateGameState(const nlohmann::json &state, std::string &error) {
  if (!state.is_object()) {
    error = "game save state must be an object";
    return false;
  }
  for (const char *section :
       {"game", "selected_entities", "prefab_instances", "lua"}) {
    if (!state.contains(section) || !state[section].is_object()) {
      error =
          std::string("game save state requires object section: ") + section;
      return false;
    }
  }
  return true;
}

std::optional<nlohmann::json>
buildGameSaveDocument(std::string slot, const nlohmann::json &state,
                      const int formatVersion, const GameSaveMetadata &metadata,
                      std::string &error) {
  if (slot.empty()) {
    error = "game save slot cannot be empty";
    return std::nullopt;
  }
  if (formatVersion < 1 || formatVersion > CurrentGameSaveVersion) {
    error = "unsupported game save format_version " +
            std::to_string(formatVersion) + "; runtime supports versions 1-" +
            std::to_string(CurrentGameSaveVersion);
    return std::nullopt;
  }
  if (!validateGameState(state, error))
    return std::nullopt;
  return nlohmann::json{{"format_version", formatVersion},
                        {"slot", std::move(slot)},
                        {"state_model", "game_state_v1"},
                        {"metadata",
                         {{"autosave", metadata.autosave},
                          {"sequence", metadata.sequence},
                          {"reason", metadata.reason}}},
                        {"state", state}};
}

bool validateGameSaveDocument(const nlohmann::json &document,
                              std::string &error) {
  if (!document.is_object() ||
      document.value("state_model", std::string{}) != "game_state_v1") {
    error = "save is not an explicit game_state_v1 document";
    return false;
  }
  const int version = document.value("format_version", 0);
  if (version < 1 || version > CurrentGameSaveVersion) {
    error = "incompatible save format_version " + std::to_string(version) +
            "; runtime supports versions 1-" +
            std::to_string(CurrentGameSaveVersion);
    return false;
  }
  if (!document.contains("metadata") || !document["metadata"].is_object()) {
    error = "game save requires metadata";
    return false;
  }
  if (!document.contains("state")) {
    error = "game save requires state";
    return false;
  }
  return validateGameState(document["state"], error);
}

} // namespace demi::runtime::persistence
