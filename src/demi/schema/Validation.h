#pragma once

#include "demi/diagnostics/Diagnostic.h"

#include <filesystem>
#include <string>
#include <vector>

namespace demi {

enum class SourceFileKind {
  Unknown,
  Project,
  Scene,
  Save,
  Asset,
};

struct ValidationSummary {
  int checkedFiles = 0;
  Diagnostics diagnostics;
};

[[nodiscard]] SourceFileKind classifySourceFile(const std::filesystem::path& path);
[[nodiscard]] ValidationSummary validatePath(const std::filesystem::path& path);
[[nodiscard]] Diagnostics validateTextFile(const std::filesystem::path& path, SourceFileKind kind);
[[nodiscard]] std::vector<std::string> extractSceneReferences(const std::filesystem::path& projectPath);

} // namespace demi
