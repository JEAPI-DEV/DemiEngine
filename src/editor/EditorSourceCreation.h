#pragma once
#include <filesystem>
#include <string>
namespace demi::editor {
class EditorWorkspace;
enum class EditorSourceKind {
  Scene2D,
  Scene3D,
  Hud,
  Prefab,
  UiPrefab,
  Lua,
  Material,
  Data
};
bool createEditorSource(EditorWorkspace &workspace, EditorSourceKind kind,
                        const std::string &name, std::filesystem::path &created,
                        std::string &error);
} // namespace demi::editor
