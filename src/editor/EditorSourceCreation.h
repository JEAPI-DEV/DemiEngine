#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
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
  Terrain,
  TerrainFromSelection,
  Data,
  TerrainMaterial,
  TerrainMaterialSet,
  TerrainPalette
};
struct EditorSourceDescription {
  std::string_view label;
  std::string_view folder;
  std::string_view suffix;
  bool assetDirectory = false;
};
[[nodiscard]] EditorSourceDescription
editorSourceDescription(EditorSourceKind kind);
[[nodiscard]] std::filesystem::path
editorSourceDirectory(EditorSourceKind kind,
                      const std::filesystem::path &selectedFolder);
[[nodiscard]] std::filesystem::path
editorSourceRelativePath(EditorSourceKind kind, std::string_view name,
                         const std::filesystem::path &directory = {});

struct EditorSourceAssetOptions {
  bool replaceSelectionWithPrefab = true;
  std::string initialRole;
  std::string initialAsset;
  std::string initialPrefab;
};
struct EditorSourceAssetChoices {
  std::vector<std::string> materials;
  std::vector<std::string> models;
  std::vector<std::string> prefabs;
};
[[nodiscard]] EditorSourceAssetChoices
editorSourceAssetChoices(const EditorWorkspace &workspace);
bool createEditorSource(EditorWorkspace &workspace, EditorSourceKind kind,
                        const std::string &name, std::filesystem::path &created,
                        std::string &error,
                        std::string_view selectedEntity = {},
                        std::filesystem::path destinationDirectory = {},
                        const EditorSourceAssetOptions &assetOptions = {});
} // namespace demi::editor
