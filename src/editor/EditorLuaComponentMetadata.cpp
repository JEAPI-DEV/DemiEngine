#include "editor/EditorLuaComponentMetadata.h"

#include "demi/runtime/scripting/ScriptComponentMetadata.h"
#include "demi/runtime/scripting/ScriptPropertyContract.h"

#include <algorithm>
#include <tuple>
#include <unordered_set>

namespace demi::editor {
namespace {

void reject(Diagnostic *diagnostic, const std::filesystem::path &path,
            std::string message) {
  if (diagnostic != nullptr)
    *diagnostic = {.severity = Severity::Error,
                   .code = "EDITOR_LUA_COMPONENT_METADATA_INVALID",
                   .message = std::move(message),
                   .path = path.string(),
                   .suggestion =
                       "Fix the @demi_component JSON header or remove it."};
}

std::string moduleUri(const std::filesystem::path &source,
                      const std::filesystem::path &projectDirectory) {
  std::error_code error;
  const std::filesystem::path relative =
      std::filesystem::relative(source, projectDirectory, error);
  if (error || relative.empty() || relative.string().starts_with(".."))
    return {};
  return "script://" + relative.generic_string();
}

} // namespace

std::optional<EditorLuaComponentMetadata>
parseEditorLuaComponentMetadata(const std::filesystem::path &sourcePath,
                                const std::filesystem::path &projectDirectory,
                                Diagnostic *diagnostic) {
  if (sourcePath.extension() != ".lua")
    return std::nullopt;
  try {
    std::string metadataError;
    const auto parsed =
        runtime::parseScriptComponentMetadata(sourcePath, metadataError);
    if (!parsed) {
      if (!metadataError.empty())
        reject(diagnostic, sourcePath, metadataError);
      return std::nullopt;
    }
    const std::string module = moduleUri(sourcePath, projectDirectory);
    EditorLuaComponentMetadata metadata{
        .id = module.starts_with("script://")
                  ? "script-component://" + module.substr(9)
                  : std::string{},
        .displayName = parsed->displayName,
        .category = parsed->category,
        .description = parsed->description,
        .module = module,
        .propertySchema = parsed->propertySchema,
        .sourcePath = sourcePath};
    if (metadata.module.empty()) {
      reject(diagnostic, sourcePath,
             "Lua component source is outside the project.");
      return std::nullopt;
    }
    std::string propertyError;
    const auto defaults = runtime::resolveScriptProperties(
        metadata.propertySchema, nlohmann::json::object(), propertyError);
    if (!defaults) {
      reject(diagnostic, sourcePath, propertyError);
      return std::nullopt;
    }
    metadata.defaultProperties = *defaults;
    return metadata;
  } catch (const nlohmann::json::exception &exception) {
    reject(diagnostic, sourcePath, exception.what());
    return std::nullopt;
  }
}

EditorLuaComponentCatalog discoverEditorLuaComponents(
    const std::filesystem::path &projectDirectory,
    const std::span<const std::filesystem::path> sources) {
  EditorLuaComponentCatalog catalog;
  std::unordered_set<std::string> ids;
  std::unordered_set<std::string> modules;
  for (const std::filesystem::path &source : sources) {
    Diagnostic diagnostic;
    auto metadata =
        parseEditorLuaComponentMetadata(source, projectDirectory, &diagnostic);
    if (!metadata) {
      if (!diagnostic.code.empty())
        catalog.diagnostics.push_back(std::move(diagnostic));
      continue;
    }
    if (!ids.insert(metadata->id).second ||
        !modules.insert(metadata->module).second) {
      catalog.diagnostics.push_back(
          {.severity = Severity::Error,
           .code = "EDITOR_LUA_COMPONENT_METADATA_DUPLICATE",
           .message = "A Lua component module was declared more than once: " +
                      metadata->module,
           .path = source.string(),
           .suggestion = "Keep one @demi_component declaration per module."});
      continue;
    }
    catalog.components.push_back(std::move(*metadata));
  }
  std::ranges::sort(catalog.components,
                    [](const auto &left, const auto &right) {
                      return std::tie(left.category, left.displayName) <
                             std::tie(right.category, right.displayName);
                    });
  return catalog;
}

} // namespace demi::editor
