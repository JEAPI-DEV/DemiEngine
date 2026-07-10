#include "demi/schema/Validation.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/filesystem/ProjectPaths.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>

namespace demi {

namespace {

std::string readFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void requireToken(Diagnostics& diagnostics,
                  const std::string& text,
                  const std::filesystem::path& path,
                  const char* token,
                  const char* code,
                  const char* message,
                  const char* suggestion) {
  if (text.find(token) != std::string::npos) {
    return;
  }

  diagnostics.push_back(Diagnostic{
    .severity = Severity::Error,
    .code = code,
    .message = message,
    .path = path.string(),
    .suggestion = suggestion,
  });
}

std::optional<std::filesystem::path> findProjectDirectory(const std::filesystem::path& path) {
  std::filesystem::path cursor = std::filesystem::is_regular_file(path) ? path.parent_path() : path;
  while (!cursor.empty()) {
    if (std::filesystem::exists(cursor / "demi.project.json")) {
      return cursor;
    }

    bool hasProjectFile = false;
    if (std::filesystem::exists(cursor)) {
      for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(cursor)) {
        if (entry.is_regular_file() && isProjectFile(entry.path())) {
          hasProjectFile = true;
          break;
        }
      }
    }
    if (hasProjectFile) {
      return cursor;
    }

    const std::filesystem::path parent = cursor.parent_path();
    if (parent == cursor) {
      break;
    }
    cursor = parent;
  }

  return std::nullopt;
}

void validateReferences(Diagnostics& diagnostics, const std::filesystem::path& path, const std::string& text) {
  const std::optional<std::filesystem::path> projectDirectory = findProjectDirectory(path);
  if (!projectDirectory.has_value()) {
    return;
  }

  const AssetRegistry registry = loadAssetRegistry(*projectDirectory);
  diagnostics.insert(diagnostics.end(), registry.diagnostics.begin(), registry.diagnostics.end());

  for (const std::string& reference : extractAssetReferences(text)) {
    const AssetManifest* asset = findAsset(registry, reference);
    if (asset == nullptr) {
      diagnostics.push_back(Diagnostic{
        .severity = Severity::Error,
        .code = "ASSET_REFERENCE_NOT_FOUND",
        .message = "Referenced asset was not found in project asset manifests: " + reference,
        .path = path.string(),
        .suggestion = "Add an .asset.json manifest under the project assets directory with this id.",
      });
      continue;
    }

    for (const std::filesystem::path& sourcePath : asset->sourcePaths) {
      if (!std::filesystem::exists(sourcePath)) {
        diagnostics.push_back(Diagnostic{
          .severity = Severity::Error,
          .code = "ASSET_SOURCE_NOT_FOUND",
          .message = "Asset source file does not exist for " + reference,
          .path = asset->manifestPath.string(),
          .suggestion = "Create the source file or update the asset manifest source path.",
        });
      }
    }
  }

  for (const std::string& reference : extractScriptReferences(text)) {
    const std::filesystem::path scriptPath = resolveScriptReference(*projectDirectory, reference);
    if (!std::filesystem::exists(scriptPath)) {
      diagnostics.push_back(Diagnostic{
        .severity = Severity::Error,
        .code = "SCRIPT_REFERENCE_NOT_FOUND",
        .message = "Referenced Lua script does not exist: " + reference,
        .path = path.string(),
        .suggestion = "Create the Lua script under the project directory or update the script:// reference.",
      });
    }
  }
}

void validateDuplicateEntityIds(Diagnostics& diagnostics, const std::filesystem::path& path, const std::string& text) {
  std::set<std::string> ids;
  std::size_t cursor = 0;
  while (true) {
    const std::size_t key = text.find("\"id\"", cursor);
    if (key == std::string::npos) {
      break;
    }
    const std::size_t colon = text.find(':', key + 4);
    const std::size_t quote = colon == std::string::npos ? std::string::npos : text.find('"', colon + 1);
    const std::size_t end = quote == std::string::npos ? std::string::npos : text.find('"', quote + 1);
    if (quote == std::string::npos || end == std::string::npos) {
      break;
    }
    const std::string id = text.substr(quote + 1, end - quote - 1);
    if (!id.starts_with("scene://") && !ids.insert(id).second) {
      diagnostics.push_back(Diagnostic{
        .severity = Severity::Error,
        .code = "SCENE_DUPLICATE_ENTITY_ID",
        .message = "Scene contains duplicate entity id: " + id,
        .path = path.string(),
        .suggestion = "Use stable unique ids for every entity.",
      });
    }
    cursor = end + 1;
  }
}

} // namespace

SourceFileKind classifySourceFile(const std::filesystem::path& path) {
  if (isProjectFile(path)) {
    return SourceFileKind::Project;
  }
  if (isSceneFile(path)) {
    return SourceFileKind::Scene;
  }
  if (isHudFile(path)) {
    return SourceFileKind::Hud;
  }
  if (isSaveFile(path)) {
    return SourceFileKind::Save;
  }
  if (isAssetFile(path)) {
    return SourceFileKind::Asset;
  }
  return SourceFileKind::Unknown;
}

ValidationSummary validatePath(const std::filesystem::path& path) {
  ValidationSummary summary;

  if (!std::filesystem::exists(path)) {
    summary.diagnostics.push_back(Diagnostic{
      .severity = Severity::Error,
      .code = "PATH_NOT_FOUND",
      .message = "Path does not exist.",
      .path = path.string(),
      .suggestion = "Pass a project, scene, save, or directory path that exists.",
    });
    return summary;
  }

  const std::vector<std::filesystem::path> files = collectKnownSourceFiles(path);
  if (files.empty()) {
    summary.diagnostics.push_back(Diagnostic{
      .severity = Severity::Warning,
      .code = "NO_SOURCE_FILES",
      .message = "No project, scene, or save files were found to validate.",
      .path = path.string(),
        .suggestion = "Add files ending in .project.json, .scene.json, .hud.json, or .save.json.",
    });
    return summary;
  }

  for (const std::filesystem::path& file : files) {
    ++summary.checkedFiles;
    Diagnostics fileDiagnostics = validateTextFile(file, classifySourceFile(file));
    summary.diagnostics.insert(summary.diagnostics.end(), fileDiagnostics.begin(), fileDiagnostics.end());
  }

  return summary;
}

Diagnostics validateTextFile(const std::filesystem::path& path, const SourceFileKind kind) {
  Diagnostics diagnostics;
  if (kind == SourceFileKind::Unknown) {
    diagnostics.push_back(Diagnostic{
      .severity = Severity::Warning,
      .code = "UNKNOWN_SOURCE_KIND",
      .message = "File is not a known DemiEngine source data file.",
      .path = path.string(),
        .suggestion = "Use a .project.json, .scene.json, .hud.json, or .save.json suffix.",
    });
    return diagnostics;
  }

  std::ifstream input(path);
  if (!input) {
    diagnostics.push_back(Diagnostic{
      .severity = Severity::Error,
      .code = "FILE_READ_FAILED",
      .message = "Failed to open file for validation.",
      .path = path.string(),
      .suggestion = "Check file permissions and path spelling.",
    });
    return diagnostics;
  }

  const std::string text = readFile(path);
  requireToken(diagnostics,
               text,
               path,
               "\"format_version\"",
               "MISSING_FORMAT_VERSION",
               "Source data file is missing format_version.",
               "Add an integer format_version field at the top level.");

  switch (kind) {
  case SourceFileKind::Project:
    requireToken(diagnostics, text, path, "\"name\"", "PROJECT_MISSING_NAME", "Project file is missing name.", "Add a top-level name field.");
    requireToken(diagnostics, text, path, "\"scenes\"", "PROJECT_MISSING_SCENES", "Project file is missing scenes.", "Add a scenes array with scene:// references.");
    break;
  case SourceFileKind::Scene:
    requireToken(diagnostics, text, path, "\"id\"", "SCENE_MISSING_ID", "Scene file is missing id.", "Add a stable scene id such as scene://main.");
    requireToken(diagnostics, text, path, "\"entities\"", "SCENE_MISSING_ENTITIES", "Scene file is missing entities.", "Add an entities array, even if it is empty.");
    validateDuplicateEntityIds(diagnostics, path, text);
    validateReferences(diagnostics, path, text);
    break;
  case SourceFileKind::Hud:
    requireToken(diagnostics, text, path, "\"elements\"", "HUD_MISSING_ELEMENTS", "HUD file is missing elements.", "Add an elements array, even if it is empty.");
    validateReferences(diagnostics, path, text);
    break;
  case SourceFileKind::Save:
    requireToken(diagnostics, text, path, "\"slot\"", "SAVE_MISSING_SLOT", "Save file is missing slot.", "Add a stable save slot name.");
    requireToken(diagnostics, text, path, "\"state\"", "SAVE_MISSING_STATE", "Save file is missing state.", "Add a state object for game-specific data.");
    break;
  case SourceFileKind::Asset:
    requireToken(diagnostics, text, path, "\"id\"", "ASSET_MISSING_ID", "Asset manifest is missing id.", "Add an id such as asset://sprites/player.");
    requireToken(diagnostics, text, path, "\"type\"", "ASSET_MISSING_TYPE", "Asset manifest is missing type.", "Add a type such as Texture2D.");
    if (text.find("\"type\": \"ImageAnimation2D\"") != std::string::npos) {
      requireToken(diagnostics, text, path, "\"sources\"", "ASSET_MISSING_SOURCES", "ImageAnimation2D asset is missing sources.", "Add a sources array in playback order.");
    } else {
      requireToken(diagnostics, text, path, "\"source\"", "ASSET_MISSING_SOURCE", "Asset manifest is missing source.", "Add a source file path relative to the manifest.");
    }
    {
      Diagnostic diagnostic;
      const std::optional<AssetManifest> asset = loadAssetManifest(path, &diagnostic);
      if (!asset.has_value()) {
      diagnostics.push_back(diagnostic);
      } else if (std::ranges::any_of(asset->sourcePaths, [](const std::filesystem::path& sourcePath) { return !std::filesystem::exists(sourcePath); })) {
        diagnostics.push_back(Diagnostic{
          .severity = Severity::Error,
          .code = "ASSET_SOURCE_NOT_FOUND",
          .message = "Asset source file does not exist.",
          .path = path.string(),
          .suggestion = "Create the source file or update the asset manifest source path.",
        });
      } else if (asset->texturePath.has_value() && !std::filesystem::exists(*asset->texturePath)) {
        diagnostics.push_back(Diagnostic{
          .severity = Severity::Error,
          .code = "ASSET_TEXTURE_NOT_FOUND",
          .message = "Asset texture file does not exist.",
          .path = path.string(),
          .suggestion = "Create the texture file or update the asset manifest texture path.",
        });
      } else if (asset->atlasPath.has_value() && !std::filesystem::exists(*asset->atlasPath)) {
        diagnostics.push_back(Diagnostic{
          .severity = Severity::Error,
          .code = "ASSET_ATLAS_NOT_FOUND",
          .message = "Asset atlas file does not exist.",
          .path = path.string(),
          .suggestion = "Create the atlas file or update the asset manifest atlas path.",
        });
      }
    }
    break;
  case SourceFileKind::Unknown:
    break;
  }

  return diagnostics;
}

std::vector<std::string> extractSceneReferences(const std::filesystem::path& projectPath) {
  std::vector<std::string> scenes;
  const std::string text = readFile(projectPath);

  std::size_t cursor = 0;
  while (true) {
    const std::size_t found = text.find("scene://", cursor);
    if (found == std::string::npos) {
      break;
    }

    std::size_t end = found;
    while (end < text.size()) {
      const char c = text[end];
      if (c == '"' || c == '\'' || c == ',' || c == ']' || c == '}' || c == '\n' || c == '\r' || c == ' ' || c == '\t') {
        break;
      }
      ++end;
    }

    const std::string reference = text.substr(found, end - found);
    if (std::ranges::find(scenes, reference) == scenes.end()) {
      scenes.push_back(reference);
    }
    cursor = end;
  }

  return scenes;
}

} // namespace demi
