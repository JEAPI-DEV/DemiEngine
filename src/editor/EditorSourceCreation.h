#pragma once
#include <filesystem>
#include <string>
#include <string_view>
namespace demi::editor {
class EditorWorkspace;
enum class EditorSourceKind {
  Scene2D,
  Scene3D,
  Hud,
  Prefab,
  Prefab2D,
  PrefabFromSelection,
  UiPrefab,
  Lua,
  Material,
  Data
};
bool createEditorSource(EditorWorkspace &workspace, EditorSourceKind kind,
                        const std::string &name, std::filesystem::path &created,
                        std::string &error,
                        std::string_view selectedEntity = {},
                        std::filesystem::path destinationDirectory = {});
} // namespace demi::editor
