#pragma once
#include "editor/EditorPreferencesStore.h"
#include <filesystem>
namespace demi::editor {
std::vector<std::string> codeEditorCommand(const EditorPreferences &preferences,
                                           const std::filesystem::path &project,
                                           const std::filesystem::path &file);
bool prepareCodeEditorWorkspace(const std::filesystem::path &project,
                                const std::filesystem::path &stubSource,
                                std::string &error);
} // namespace demi::editor
