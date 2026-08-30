#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace demi::editor {

struct EditorAssetDropSuggestion {
  std::filesystem::path source;
  std::string assetId;
};

[[nodiscard]] std::optional<EditorAssetDropSuggestion>
suggestAssetImport(const std::filesystem::path &source, std::string &error);

} // namespace demi::editor
