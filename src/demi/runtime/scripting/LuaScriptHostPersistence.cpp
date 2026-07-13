#include "demi/runtime/scripting/LuaScriptHost.h"

#include "demi/runtime/scripting/persistence/GameSaveDocument.h"
#include "demi/runtime/scripting/persistence/LuaSaveCodec.h"

#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <system_error>

#include <nlohmann/json.hpp>

namespace demi::runtime {

std::unordered_map<std::string, LuaScriptHost::SaveValue> &
LuaScriptHost::loadSaveSlot(const std::string &slot) {
  const std::string safeSlot = sanitizedSaveSlot(slot);
  if (auto found = saves_.find(safeSlot); found != saves_.end()) {
    return found->second;
  }

  std::unordered_map<std::string, SaveValue> values;
  const std::filesystem::path path = savePath(projectDirectory_, safeSlot);
  std::ifstream input(path);
  if (input) {
    std::ostringstream buffer;
    buffer << input.rdbuf();
    values = parseSaveState(buffer.str());
  }

  auto [inserted, _] = saves_.emplace(safeSlot, std::move(values));
  return inserted->second;
}

bool LuaScriptHost::writeSaveSlot(const std::string &slot) {
  if (projectDirectory_.empty()) {
    return false;
  }

  const std::string safeSlot = sanitizedSaveSlot(slot);
  auto found = saves_.find(safeSlot);
  if (found == saves_.end()) {
    return false;
  }

  return atomicWriteText(savePath(projectDirectory_, safeSlot),
                         serializeSaveSlotDocument(safeSlot, found->second));
}

std::optional<float> LuaScriptHost::saveNumber(const std::string &slot,
                                               const std::string &key) {
  std::unordered_map<std::string, SaveValue> &values = loadSaveSlot(slot);
  const auto found = values.find(key);
  if (found == values.end() || !found->second.number) {
    return std::nullopt;
  }
  try {
    return std::stof(found->second.value);
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<std::string> LuaScriptHost::saveString(const std::string &slot,
                                                     const std::string &key) {
  std::unordered_map<std::string, SaveValue> &values = loadSaveSlot(slot);
  const auto found = values.find(key);
  if (found == values.end() || found->second.number) {
    return std::nullopt;
  }
  return found->second.value;
}

bool LuaScriptHost::setSaveNumber(const std::string &slot,
                                  const std::string &key, const float value) {
  std::unordered_map<std::string, SaveValue> &values = loadSaveSlot(slot);
  std::ostringstream stream;
  stream << std::setprecision(6) << value;
  values[key] = SaveValue{.value = stream.str(), .number = true};
  return writeSaveSlot(slot);
}

bool LuaScriptHost::setSaveString(const std::string &slot,
                                  const std::string &key,
                                  const std::string &value) {
  std::unordered_map<std::string, SaveValue> &values = loadSaveSlot(slot);
  values[key] = SaveValue{.value = value, .number = false};
  return writeSaveSlot(slot);
}

bool LuaScriptHost::saveExists(const std::string &slot) const {
  return !projectDirectory_.empty() &&
         std::filesystem::exists(savePath(projectDirectory_, slot));
}

bool LuaScriptHost::deleteSave(const std::string &slot) {
  if (projectDirectory_.empty()) {
    return false;
  }
  std::error_code error;
  const bool removed =
      std::filesystem::remove(savePath(projectDirectory_, slot), error);
  saves_.erase(sanitizedSaveSlot(slot));
  return removed && !error;
}

std::optional<std::string>
LuaScriptHost::readSaveDocument(const std::string &slot) const {
  if (projectDirectory_.empty()) {
    return std::nullopt;
  }
  std::ifstream input(savePath(projectDirectory_, slot));
  if (!input) {
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (!parseSaveDocument(buffer.str()).has_value()) {
    return std::nullopt;
  }
  return buffer.str();
}

bool LuaScriptHost::writeSaveDocument(const std::string &slot,
                                      const std::string &stateJson,
                                      const int formatVersion) {
  if (projectDirectory_.empty() || formatVersion < 1) {
    return false;
  }

  nlohmann::json state;
  try {
    state = nlohmann::json::parse(stateJson);
  } catch (...) {
    return false;
  }
  if (!state.is_object()) {
    return false;
  }

  const std::string safeSlot = sanitizedSaveSlot(slot);
  nlohmann::json document = {
      {"format_version", formatVersion},
      {"slot", safeSlot},
      {"state", std::move(state)},
  };
  if (const auto existingText = readSaveDocument(safeSlot)) {
    if (const auto existing = parseSaveDocument(*existingText);
        existing && existing->contains("state_model")) {
      document["state_model"] = (*existing)["state_model"];
      if (existing->contains("metadata"))
        document["metadata"] = (*existing)["metadata"];
    }
  }
  saves_.erase(safeSlot);
  return atomicWriteText(savePath(projectDirectory_, safeSlot),
                         document.dump(2) + "\n");
}

bool LuaScriptHost::writeGameSaveDocument(const std::string &slot,
                                          const std::string &stateJson,
                                          const int formatVersion,
                                          const bool autosave,
                                          const int sequence,
                                          const std::string &reason) {
  lastSaveError_.clear();
  nlohmann::json state;
  try {
    state = nlohmann::json::parse(stateJson);
  } catch (const std::exception &exception) {
    lastSaveError_ =
        "invalid game save state JSON: " + std::string(exception.what());
    return false;
  }
  const std::string safeSlot = sanitizedSaveSlot(slot);
  const auto document = persistence::buildGameSaveDocument(
      safeSlot, state, formatVersion,
      {.autosave = autosave, .sequence = sequence, .reason = reason},
      lastSaveError_);
  if (!document)
    return false;
  saves_.erase(safeSlot);
  if (!atomicWriteText(savePath(projectDirectory_, safeSlot),
                       document->dump(2) + "\n")) {
    lastSaveError_ = "failed to atomically write game save";
    return false;
  }
  return true;
}

std::optional<std::string>
LuaScriptHost::readGameSaveDocument(const std::string &slot) {
  lastSaveError_.clear();
  const auto text = readSaveDocument(slot);
  if (!text) {
    lastSaveError_ = "game save does not exist or is malformed";
    return std::nullopt;
  }
  const auto document = parseSaveDocument(*text);
  if (!document ||
      !persistence::validateGameSaveDocument(*document, lastSaveError_))
    return std::nullopt;
  return text;
}

const std::string &LuaScriptHost::lastSaveError() const {
  return lastSaveError_;
}

int LuaScriptHost::saveFormatVersion(const std::string &slot) const {
  const std::optional<std::string> documentText = readSaveDocument(slot);
  if (!documentText.has_value()) {
    return 0;
  }
  const std::optional<nlohmann::json> document =
      parseSaveDocument(*documentText);
  if (!document.has_value() ||
      !(*document)["format_version"].is_number_integer()) {
    return 0;
  }
  return (*document)["format_version"].get<int>();
}

void LuaScriptHost::addSaveMigrationHook(const int fromVersion,
                                         const int toVersion,
                                         const int callbackRef) {
  if (fromVersion < 1 || toVersion <= fromVersion || callbackRef == 0) {
    return;
  }
  saveMigrationHooks_.push_back(SaveMigrationHook{.fromVersion = fromVersion,
                                                  .toVersion = toVersion,
                                                  .callbackRef = callbackRef});
}

const std::vector<LuaScriptHost::SaveMigrationHook> &
LuaScriptHost::saveMigrationHooks() const {
  return saveMigrationHooks_;
}

} // namespace demi::runtime
