#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace demi {

enum class SourceFileKind {
  Unknown,
  Project,
  Scene,
  Hud,
  Save,
  Asset,
  Prefab,
  UiPrefab,
  InputReplay,
  Package,
  AssetGroup,
};

struct ValidationSummary {
  int checkedFiles = 0;
  Diagnostics diagnostics;
};

[[nodiscard]] SourceFileKind
classifySourceFile(const std::filesystem::path &path);
[[nodiscard]] ValidationSummary validatePath(const std::filesystem::path &path);
[[nodiscard]] Diagnostics validateTextFile(const std::filesystem::path &path,
                                           SourceFileKind kind);
// Validates an in-memory scene document with the same checks `demi validate`
// runs for a scene: duplicate entity ids, component registry fields, prefab
// expansion, Transform2D/Transform3D hierarchies, and 3D physics conflicts.
// Cross-file asset and script references are resolved separately when the
// document is saved.
[[nodiscard]] Diagnostics
validateSceneDocument(const std::filesystem::path &scenePath,
                      const nlohmann::json &document);
[[nodiscard]] std::vector<std::string>
extractSceneReferences(const std::filesystem::path &projectPath);

} // namespace demi
