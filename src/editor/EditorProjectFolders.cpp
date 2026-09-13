#include "editor/EditorProjectFolders.h"

#include <algorithm>
#include <cctype>

namespace demi::editor {

bool isEditorInternalDirectory(const std::string_view name) {
  return name == "generated" || name == "build" || name == ".demi" ||
         name == ".git";
}

bool createEditorProjectFolder(const std::filesystem::path &projectDirectory,
                               const std::filesystem::path &relativeParent,
                               const std::string_view name,
                               std::string &error) {
  error.clear();
  if (name.empty() || name == "." || name == ".." ||
      name.find_first_of("/\\") != std::string_view::npos ||
      std::ranges::any_of(name,
                          [](unsigned char c) { return c < 32 || c == 127; }) ||
      std::ranges::all_of(name,
                          [](unsigned char c) { return std::isspace(c); })) {
    error = "Enter a folder name, not a path (no slashes, dot entries, or "
            "control characters).";
    return false;
  }
  if (isEditorInternalDirectory(name) || relativeParent.has_root_path()) {
    error = "Folders must be created in the project's authored directories.";
    return false;
  }
  std::error_code code;
  const auto root = std::filesystem::canonical(projectDirectory, code);
  if (code || !std::filesystem::is_directory(root, code)) {
    error = "The project directory is unavailable.";
    return false;
  }
  auto parent = root;
  for (const auto &part : relativeParent) {
    if (part == "." || part.empty())
      continue;
    if (part == ".." || isEditorInternalDirectory(part.string())) {
      error = "Cannot create folders outside authored project directories.";
      return false;
    }
    parent /= part;
    const auto status = std::filesystem::symlink_status(parent, code);
    if (code || !std::filesystem::is_directory(status)) {
      error = "The parent must be an existing directory, not a symbolic link.";
      return false;
    }
  }
  const auto resolvedParent = std::filesystem::canonical(parent, code);
  const auto relative = resolvedParent.lexically_relative(root);
  if (code || relative.empty() || relative.is_absolute() ||
      (!relative.empty() && *relative.begin() == "..")) {
    error = "The selected parent is outside the project.";
    return false;
  }
  if (!std::filesystem::create_directory(resolvedParent / std::string(name),
                                         code)) {
    error = code ? "Could not create folder: " + code.message()
                 : "A folder with that name already exists.";
    return false;
  }
  return true;
}

} // namespace demi::editor
