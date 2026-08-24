#include "editor/EditorAssetDrop.h"

#include <algorithm>
#include <cctype>

namespace demi::editor {

std::optional<EditorAssetDropSuggestion>
suggestAssetImport(const std::filesystem::path &source, std::string &error) {
  std::error_code filesystemError;
  if (!std::filesystem::is_regular_file(source, filesystemError) ||
      filesystemError) {
    error = "Only regular files can be dropped into the asset importer.";
    return std::nullopt;
  }
  std::string name = source.stem().string();
  std::ranges::transform(name, name.begin(), [](const unsigned char value) {
    return static_cast<char>(std::tolower(value));
  });
  for (char &value : name)
    if (!std::isalnum(static_cast<unsigned char>(value)) && value != '-' &&
        value != '_')
      value = '_';
  if (name.empty()) {
    error = "The dropped file does not produce a usable asset ID.";
    return std::nullopt;
  }
  return EditorAssetDropSuggestion{
      .source = std::filesystem::absolute(source).lexically_normal(),
      .assetId = "asset://" + name};
}

} // namespace demi::editor
