#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace demi::editor {

[[nodiscard]] bool isEditorInternalDirectory(std::string_view name);
[[nodiscard]] bool
createEditorProjectFolder(const std::filesystem::path &projectDirectory,
                          const std::filesystem::path &relativeParent,
                          std::string_view name, std::string &error);

} // namespace demi::editor
