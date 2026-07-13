#include "demi/runtime/scripting/persistence/GameSaveDocument.h"

#include <iostream>

#include <nlohmann/json.hpp>

int main() {
  using namespace demi::runtime::persistence;

  const nlohmann::json state = {
      {"game", {{"score", 12}}},
      {"selected_entities", {{"player", "entity://player"}}},
      {"prefab_instances", {{"enemy_1", "prefab://enemy"}}},
      {"lua", {{"quest", {{"stage", 2}}}}},
  };
  std::string error;
  const auto document = buildGameSaveDocument(
      "autosave", state, CurrentGameSaveVersion,
      {.autosave = true, .sequence = 4, .reason = "checkpoint"}, error);
  if (!document || !validateGameSaveDocument(*document, error)) {
    std::cerr << "valid structured save rejected: " << error << '\n';
    return 1;
  }
  if ((*document)["metadata"]["sequence"] != 4 ||
      (*document)["state"]["game"]["score"] != 12) {
    std::cerr << "structured save did not preserve state or metadata\n";
    return 1;
  }

  auto incompatible = *document;
  incompatible["format_version"] = CurrentGameSaveVersion + 1;
  if (validateGameSaveDocument(incompatible, error) ||
      error.find("incompatible") == std::string::npos) {
    std::cerr << "incompatible save did not produce a clear diagnostic\n";
    return 1;
  }

  auto incomplete = state;
  incomplete.erase("lua");
  if (buildGameSaveDocument("broken", incomplete, CurrentGameSaveVersion, {},
                            error)) {
    std::cerr << "incomplete structured state was accepted\n";
    return 1;
  }
  return 0;
}
