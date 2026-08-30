#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace demi::editor {

struct EditorLuaComponentMetadata {
  std::string id;
  std::string displayName;
  std::string category;
  std::string description;
  std::string module;
  nlohmann::json propertySchema = nlohmann::json::object();
  nlohmann::json defaultProperties = nlohmann::json::object();
  std::filesystem::path sourcePath;
};

struct EditorLuaComponentCatalog {
  std::vector<EditorLuaComponentMetadata> components;
  Diagnostics diagnostics;
};

[[nodiscard]] std::optional<EditorLuaComponentMetadata>
parseEditorLuaComponentMetadata(const std::filesystem::path &sourcePath,
                                const std::filesystem::path &projectDirectory,
                                Diagnostic *diagnostic = nullptr);
[[nodiscard]] EditorLuaComponentCatalog
discoverEditorLuaComponents(const std::filesystem::path &projectDirectory,
                            std::span<const std::filesystem::path> sources);

} // namespace demi::editor
