#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <string>

namespace demi::runtime {

struct ScriptComponentMetadata {
  std::string displayName;
  std::string category;
  std::string description;
  nlohmann::json propertySchema = nlohmann::json::object();
};

// Returns no value and an empty error when the script declares no component
// header. A non-empty error means a header was present but invalid.
[[nodiscard]] std::optional<ScriptComponentMetadata>
parseScriptComponentMetadata(const std::filesystem::path &path,
                             std::string &error);

} // namespace demi::runtime
